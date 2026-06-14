# 09 — Статика: Path / Filesystem / FilesystemHandler / Autoindex / Mime

## Назначение

Группа модулей, отвечающих за отдачу файлов с диска: безопасно превратить URI в путь, классифицировать его
(файл / каталог / нет / запрещён), прочитать файл или сгенерировать листинг каталога, подобрать `Content-Type`.

| Модуль | Роль | Где |
|---|---|---|
| `Path` | URI-хелперы + **безопасное** склеивание пути (защита от traversal) | `src/Path.cpp` |
| `Filesystem` (`Fs`) | `stat`-классификация и чтение файла | `src/Filesystem.cpp` |
| `FilesystemHandler` | оркестратор: URI → `HttpReply` | `src/FilesystemHandler.cpp` |
| `Autoindex` | HTML-листинг каталога | `src/Autoindex.cpp` |
| `Mime` | расширение → `Content-Type` | `src/Mime.cpp` |

## Диаграмма: обработка GET статики

```mermaid
flowchart TD
    U[uri + EffectiveConfig] --> A{uri == "/"?}
    A -->|да| IDX["root/index → отдать или 403"]
    A -->|нет| J{alias или root?}
    J -->|alias| SJA["safeJoinAlias(alias, prefix, uri)"]
    J -->|root| SJ["safeJoin(root, uri)"]
    SJA & SJ -->|403/400| ERR[makeErrorReply]
    SJA & SJ --> CL[Fs::classifyPath]
    CL -->|MISSING/FORBIDDEN/ERROR| ERRk["makeErrorReply(404/403/500)"]
    CL -->|DIR| D{слэш в конце?}
    D -->|нет| RED[301 → uri + '/']
    D -->|да| IH{index файл есть?}
    IH -->|да| FILE[прочитать index → 200]
    IH -->|нет| AI{autoindex on?}
    AI -->|да| LIST[appendDirectoryListingHtml → 200 text/html]
    AI -->|нет| E404[makeErrorReply 404]
    CL -->|FILE| RF[readFileToString → makeOkReply + guessContentType]
```

## Сниппет: безопасное склеивание пути (`safeJoin`)

Это **самая важная для безопасности** функция: блокирует path traversal (`../`), в т.ч. URL-encoded.

```cpp
// src/Path.cpp:132  (сокращённо)
bool safeJoin(const std::string &root, const std::string &rawUri,
              std::string &outFsPath, int &outStatus)
{
    std::string uriNoQuery = uriPathOnly(rawUri);     // отрезаем "?query"
    std::string decoded;
    if (!urlDecodePath(uriNoQuery, decoded)) { outStatus = 400; return false; }  // %2e%2e → ..
    if (decoded.empty() || decoded[0] != '/') { outStatus = 400; return false; }

    std::vector<std::string> segments;                // разбиваем по '/' и нормализуем
    // ... для каждого сегмента:
    //   ""  или "."  → пропустить
    //   ".."          → segments.pop_back(); если пусто — это выход за root:
    if (current == "..") {
        if (segments.empty()) { outStatus = 403; return false; }  // ← traversal заблокирован
        segments.pop_back();
    } else segments.push_back(current);

    outFsPath = root;                                 // собираем безопасный путь внутри root
    for (std::size_t i = 0; i < segments.size(); ++i)
        outFsPath = Fs::joinPath(outFsPath, segments[i]);
    outStatus = 200; return true;
}
```

**Объяснение.** Декодирование происходит **до** нормализации, поэтому `/%2e%2e/etc/passwd` превращается в
`/../etc/passwd` и блокируется так же, как обычные `../`. Когда `..` пытается «всплыть» выше root, `segments`
уже пуст → 403. `safeJoinAlias` делает то же, но заменяет префикс location на каталог `alias` (для
`location /directory/ { alias ./YoupiBanane/; }`).

## Сниппет: классификация пути через `stat`

```cpp
// src/Filesystem.cpp:79
PathKind classifyPath(const std::string &path) {
    struct stat st;
    if (::stat(path.c_str(), &st) == 0)
        return S_ISDIR(st.st_mode) ? PATH_DIR : PATH_FILE;
    if (errno == ENOENT || errno == ENOTDIR) return PATH_MISSING;   // → 404
    if (errno == EACCES)                     return PATH_FORBIDDEN;  // → 403
    return PATH_ERROR;                                               // → 500
}
```

## Сниппет: подбор Content-Type

```cpp
// src/Mime.cpp  guessContentType — по расширению, lower-case
if (ext == "html" || ext == "htm") return "text/html";
if (ext == "css")  return "text/css";
if (ext == "png")  return "image/png";
// ... иначе:
return "application/octet-stream";
```

## На что смотреть на ревью / типичные баги

- **Path traversal**: `GET /../../etc/passwd` и `GET /%2e%2e/...` должны давать 4xx, а не выдавать системные
  файлы (TC-11). Это критично — проверяется в первую очередь.
- **Редирект каталога**: `/dir` без слэша → 301 на `/dir/` (`FilesystemHandler.cpp:81`), чтобы относительные
  ссылки в листинге/HTML работали.
- **index vs autoindex**: если в каталоге есть `index` — отдаётся он; если нет, но `autoindex on` — листинг;
  иначе 404 (`FilesystemHandler.cpp:84-121`).
- **`Content-Type`**: html отдаётся как `text/html`, неизвестные расширения — `application/octet-stream`.
- **alias vs root**: при `alias` обязателен `loc` (иначе 500, `FilesystemHandler.cpp:50`); путь строится
  через `safeJoinAlias`, а не `safeJoin`.
- **Чтение в память**: `readFileToString` грузит файл целиком — для больших файлов используется потоковый путь
  в `Connection` (см. [`06`](06-connection.md)), чтобы не держать гигабайты в RAM.

---

Дальше: [`10-cgi.md`](10-cgi.md) — запуск внешних скриптов.
