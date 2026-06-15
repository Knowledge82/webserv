
## 1. Конфигурационный файл

> «Your program must use a configuration file, provided as an argument on the command line,
> or available in a default path.»

**Зачем это нужно.** Сервер не должен иметь «зашитых» (hardcoded) параметров. Он обязан
гибко настраиваться под разные сайты, порты и директории через внешний файл — как `nginx`.

**Как это сделано.** В `src/main.cpp` по количеству аргументов выбирается источник конфигурации:
путь к файлу → `ConfigLoader::loadFromFile`, иначе → `ConfigLoader::loadDefault` (порт 8080 в
памяти). Лексику разбирают `ConfigTokenizer` (токены) и `ConfigParser` (объекты + валидация).

**Сниппет** (`src/main.cpp:34-57`):

```cpp
Config cfg;
if (argc == 1)
    cfg = ConfigLoader::loadDefault();                 // дефолт в памяти (порт 8080)
else if (argc == 2)
{
    if (std::string(argv[1]) == "--check-config")      // только проверить и выйти
    {
        cfg = ConfigLoader::loadDefault();
        std::cout << GREEN << "OK: " << RESET << "default config" << std::endl;
        return 0;
    }
    cfg = ConfigLoader::loadFromFile(argv[1]);          // ./webserv conf/tester.conf
}
else if (argc == 3)                                    // ./webserv --check-config <file>
{
    if (std::string(argv[1]) != "--check-config") return printUsage();
    cfg = ConfigLoader::loadFromFile(argv[2]);
    std::cout << GREEN << "OK: " << RESET << argv[2] << std::endl;
    return 0;
}
Server s(cfg);
s.run();
```

**Пример проверки.**

```bash
./webserv --check-config conf/tester.conf   # синтаксис ОК → "OK: conf/tester.conf", exit 0
./webserv conf/tester.conf                  # запуск сервера на порту из конфига
```

---

## 2. Несколько `interface:port` (несколько сайтов)

> «Define all the interface:port pairs on which your server will listen to (defining multiple
> websites served by your program).»

**Зачем это нужно.** Один процесс должен обслуживать несколько сайтов на разных портах/адресах
одновременно — как виртуальные хосты в nginx.

**Как это сделано.** Каждый блок `server { ... }` → `ServerConfig`, каждая директива `listen` →
`ListenConfig` (`host:port`). В `Server::run()` для **каждой** пары открывается отдельный
слушающий сокет (`FD_LISTEN`), и все они обслуживаются **единым** `poll()`.

**Пример конфига** (`conf/2serv.conf` — два сайта):

```nginx
server {
    listen 127.0.0.1:8080;
    root ./www;
}
server {
    listen 127.0.0.1:8081;
    root ./www2;
}
```

**Пример проверки.**

```bash
curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8080/   # 200 — первый сайт
curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8081/   # 200 — второй сайт
```

---

## 3. Кастомные страницы ошибок

> «Set up default error pages.»

**Зачем это нужно.** Вместо «голого» кода ошибки отдавать осмысленную страницу.

**Как это сделано.** Любая ошибка собирается через `HttpResponse::buildErrorResponse(status)`.
Если пользовательская страница не задана — формируется встроенное тело `errorBody()`, а текст
статуса берётся из `reasonPhrase` (`src/HttpResponse.cpp:26`).

**Сниппет** (`src/HttpResponse.cpp`, как код → текстовая фраза):

```cpp
static std::string reasonPhrase(int code)
{
    switch (code)
    {
        case 200: return "OK";
        case 301: return "Moved Permanently";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default:  return "Error";
    }
}
```

**Пример проверки.**

```bash
curl -s -i "$BASE/notfound" | head -1     # HTTP/1.1 404 Not Found + непустое тело
```

---

## 4. Ограничение размера тела запроса

> «Set the maximum allowed size for client request bodies.»

**Зачем это нужно.** Защита от исчерпания памяти: клиент не должен «положить» сервер гигантским
телом запроса.

**Как это сделано.** Директива `client_max_body_size` попадает в `EffectiveConfig`. В
`Connection::onReadable` размер тела сверяется с лимитом → **413 Payload Too Large**.

**Сниппет** (`src/Connection.cpp:604-609`):

```cpp
if (eff.hasClientMaxBodySize && request_.getContentLength() > eff.clientMaxBodySize)
{
    out_ = HttpResponse::buildErrorResponse(413);   // Payload Too Large
    state_ = WRITING;
    return true;
}
```

**Пример конфига** (`conf/tester.conf:11-14` — лимит 100 байт на маршруте):

```nginx
location /post_body {
    allow_methods POST;
    client_max_body_size 100;
}
```

**Пример проверки.**

```bash
PAYLOAD=$(head -c 200 /dev/zero | tr '\0' 'A')                       # 200 байт > лимита 100
curl -s -o /dev/null -w "%{http_code}\n" -X POST --data "$PAYLOAD" "$BASE/post_body"  # 413
```

---

## 5. Правила на маршруте (location)

> «Specify rules or configurations on a URL/route (no regex required here)…»

Маршрут выбирается по **самому длинному совпадающему префиксу** в `selectLocation`, после чего
настройки `server` и `location` сливаются в `buildEffectiveConfig`: значения из `location`
переопределяют значения из `server`.

**Сниппет — выбор location** (`src/Connection.cpp:59-83`):

```cpp
const LocationConfig *selectLocation(const std::vector<LocationConfig> &locations,
                                     const std::string &uri)
{
    const LocationConfig *best = NULL;
    std::size_t           bestLen = 0;
    for (std::size_t i = 0; i < locations.size(); ++i)
    {
        const std::string &prefix = locations[i].prefix;
        if (!Http::startsWithPrefix(uri, prefix)) continue;
        if (prefix.size() >= bestLen) { best = &locations[i]; bestLen = prefix.size(); }
    }
    return best;                       // самый длинный префикс
}
```

**Сниппет — слияние server → location** (`src/Connection.cpp:85-132`, фрагмент):

```cpp
EffectiveConfig buildEffectiveConfig(const ServerConfig &srv, const LocationConfig *loc)
{
    EffectiveConfig eff;
    if (srv.hasRoot)            { eff.hasRoot = true; eff.root = srv.root; }
    if (loc && loc->hasRoot)    { eff.hasRoot = true; eff.root = loc->root; }   // location важнее
    if (srv.hasIndex)           { eff.hasIndex = true; eff.index = srv.index; }
    if (loc && loc->hasIndex)   { eff.hasIndex = true; eff.index = loc->index; }
    // ... аналогично client_max_body_size, autoindex, allow_methods, upload_dir, redirect, cgi
    return eff;
}
```

### 5.1. Список разрешённых HTTP-методов

> «List of accepted HTTP methods for the route.»

**Как это сделано.** `isAllowedMethod` сверяет метод со списком `allow_methods`; иначе **405**.

**Сниппет** (`src/Connection.cpp:180-192` + проверка в `onReadable:622`):

```cpp
bool isAllowedMethod(const std::string &method, const EffectiveConfig &eff)
{
    if (!eff.hasAllowedMethods) return true;                 // нет ограничения — всё можно
    for (std::size_t i = 0; i < eff.allowedMethods.size(); ++i)
        if (eff.allowedMethods[i] == method) return true;
    return false;
}
// в onReadable:
if (!isAllowedMethod(request_.getMethod(), eff))
{
    out_ = HttpResponse::buildErrorResponse(405);            // Method Not Allowed
    state_ = WRITING; return true;
}
```

**Пример конфига** (`conf/tester.conf:7-9`): `location / { allow_methods GET; }`.

**Пример проверки.**

```bash
# location / разрешает только GET → POST даёт 405
curl -s -o /dev/null -w "%{http_code}\n" -X POST --data x "$BASE/"   # 405
```

### 5.2. HTTP-редирект

> «HTTP redirection.»

**Как это сделано.** Если задан `return`, запрос сразу получает
`HttpResponse::buildRedirectResponse(code, target)` с заголовком `Location:`.

**Сниппет** (`src/Connection.cpp:614-619`):

```cpp
if (eff.hasRedirect)
{
    out_ = HttpResponse::buildRedirectResponse(eff.redirectCode, eff.redirectTarget);
    state_ = WRITING; return true;
}
```

**Пример конфига:** `location /old { return 301 /new; }`.

**Пример проверки.**

```bash
curl -s -o /dev/null -D - "$BASE/old" | grep -i '^location'   # Location: /new
```

### 5.3. Корневая директория маршрута (root / alias)

> «Directory where the requested file should be located (e.g., if URL /kapouet is rooted to
> /tmp/www, URL /kapouet/pouic/toto/pouet will search for /tmp/www/pouic/toto/pouet).»

**Как это сделано.** URI → путь на диске через `safeJoin(root, uri)`, а при `alias` —
`safeJoinAlias`. Декодирование `%xx` выполняется **до** нормализации `..`, поэтому path traversal
(`/../../etc/passwd`, в т.ч. `%2e%2e`) блокируется кодом 403.

**Сниппет — защита от traversal** (`src/Path.cpp:185-199`):

```cpp
if (current == "..")
{
    if (segments.empty())          // .. пытается выйти выше root → нечего pop_back()
    {
        outStatus = 403;           // ← path traversal заблокирован
        return false;
    }
    segments.pop_back();
    current.clear();
    continue;
}
segments.push_back(current);       // обычный сегмент — добавляем к безопасному пути
```

**Пример конфига** (`conf/tester.conf:16-22` — alias подменяет префикс каталогом):

```nginx
location /directory/ {
    alias ./YoupiBanane/;          # /directory/foo  →  ./YoupiBanane/foo
}
```

**Пример проверки.**

```bash
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/../../etc/passwd"   # 403/400, НЕ 200
```

### 5.4. Листинг каталога (autoindex)

> «Enabling or disabling directory listing.»

**Как это сделано.** Директива `autoindex on/off`. Каталог без индекс-файла + `autoindex on` →
`Autoindex::appendDirectoryListingHtml` строит HTML-список; иначе **404**.

**Пример конфига:** `location /files/ { autoindex on; }`.

**Пример проверки.**

```bash
curl -s "$BASE/files/" | grep -i '<a href'     # видим список ссылок на файлы
```

### 5.5. Файл по умолчанию для каталога (index)

> «Default file to serve when the requested resource is a directory.»

**Как это сделано.** Директива `index`. При запросе каталога `FilesystemHandler` сначала пробует
индекс-файл, потом autoindex/404. Каталог без завершающего `/` → **301** на путь со слэшем
(`tryRedirectToSlashLocation`), чтобы относительные ссылки работали.

**Пример конфига** (`conf/tester.conf:1-5`): `index index.html;`.

**Пример проверки.**

```bash
curl -s -o /dev/null -D - "$BASE/directory" | grep -i '^location'   # 301 → /directory/
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/"                   # 200, отдан index.html
```

### 5.6. Загрузка файлов от клиента (upload)

> «Uploading files from the clients to the server is authorized, and storage location is provided.»

**Как это сделано.** Если в location задан `upload_dir`, `POST`/`PUT` идут в
`Connection::handleUpload`: тело сохраняется в каталог **циклом записи** (защита от частичного
`write`), ответ **201 Created**. Удаление — `handleDelete` для `DELETE`.

**Сниппет** (`src/Connection.cpp:441-479`, фрагмент):

```cpp
int fileFd = ::open(finalPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
if (fileFd < 0) { prepareReply(Http::makeErrorReply(500)); return true; }

const std::string &body = request_.getBody();
const char *ptr = body.data();
std::size_t bytesLeft = body.size();
while (bytesLeft > 0)                          // дописываем, пока всё тело не на диске
{
    ssize_t written = ::write(fileFd, ptr, bytesLeft);
    if (written < 0) { ::close(fileFd); ::unlink(finalPath.c_str());   // чистим мусор
                       prepareReply(Http::makeErrorReply(500)); return true; }
    ptr += written; bytesLeft -= static_cast<std::size_t>(written);
}
::close(fileFd);
prepareReply(Http::makeReply(201, "text/plain", "File uploaded successfully.\n"));
```

**Пример конфига** (`conf/upload.conf`):

```nginx
location /upload/ {
    allow_methods POST;
    upload_dir ./www/uploads/;
}
```

**Пример проверки.**

```bash
curl -s -o /dev/null -w "%{http_code}\n" -X POST --data "hello" "$BASE/upload/my.txt"  # 201
cat www/uploads/my.txt                                                                 # hello
```

### 5.7. Выполнение CGI по расширению файла

> «Execution of CGI, based on file extension (for example .php).»

**Как это сделано.** `Http::isCgiRequest` определяет CGI по расширению (карта `ext → интерпретатор`).
Запуск — `Connection::startCgi` (`fork`/`execve`/`pipe`). Ключевые гарантии сабжекта:

- **Полный запрос доступен скрипту** — переменные окружения CGI/1.1 (`prepareCgiArgs`):

```cpp
// src/CgiHandler.cpp:113-135
outEnv.push_back("GATEWAY_INTERFACE=CGI/1.1");
outEnv.push_back("SERVER_PROTOCOL=HTTP/1.1");
outEnv.push_back("REQUEST_METHOD=" + req.getMethod());
outEnv.push_back("QUERY_STRING="  + Http::uriQueryOnly(req.getUri()));
outEnv.push_back("SCRIPT_FILENAME=" + scriptFsPath);
outEnv.push_back("PATH_INFO=" + pathInfo);
if (req.getMethod() == "POST" || req.getMethod() == "PUT")
{
    std::ostringstream oss; oss << req.getContentLength();
    outEnv.push_back("CONTENT_LENGTH=" + oss.str());     // тело пойдёт скрипту на stdin
}
```

- **Chunked → un-chunk, EOF = конец тела.** Сервер разбирает `Transfer-Encoding: chunked`
  (`parseChunkedBody`); скрипту конец тела сигнализируется закрытием его `stdin` (EOF).
- **Нет Content-Length от CGI → конец по EOF.** `parseCgiOutput` читает вывод до закрытия пайпа.
- **Правильный рабочий каталог.** Перед `execve` дочерний процесс делает `chdir(workDir)`.
- **Минимум один CGI.** Поддержаны Python и Bash (мульти-CGI — бонус).

**Пример конфига** (`conf/tester.conf:24-29` — два интерпретатора):

```nginx
location /cgi-bin/ {
    allow_methods GET POST;
    cgi .py /opt/pyenv/shims/python3;
    cgi .sh /bin/bash;
}
```

**Пример проверки.**

```bash
curl -s "$BASE/cgi-bin/test.py"   # вывод Python-скрипта, статус 200
curl -s "$BASE/cgi-bin/test.sh"   # вывод Bash-скрипта,   статус 200
```

---

## 6. Устойчивость и неблокирующая работа

> «Resilience is key. Your server must remain operational at all times.»

**Как это сделано.**
- **Единственный `poll()`** обслуживает чтение/запись на всех сокетах и CGI-пайпах. `read`/`write`
  вне poll-цикла нет.
- Все сокеты неблокирующие; частичные `recv`/`send` обрабатываются (накопить в буфер, дослать на
  следующем `POLLOUT`).
- Битый/неполный запрос не роняет сервер: парсер ждёт данных, ошибки → HTTP-коды, а не краш.

**Сниппет — частичное чтение не блокирует** (`src/Connection.cpp:525-545`):

```cpp
ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
if (n == 0) return false;                       // клиент закрыл соединение
if (n < 0)  return false;                       // ошибка — Server закроет соединение
in_.append(buf, n);
HttpRequest::State st = request_.parse(in_, maxHeaderBytes, maxBodyBytes);
// пока st == HEADERS/BODY — остаёмся в READING и ждём следующий recv (не блокируемся)
```

**Пример проверки.**

```bash
printf 'GET / HTTP/1.1\r\n' | nc -w1 "$HOST" "$PORT"   # неполный запрос
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/"      # сервер жив → снова 200
```

Сборка — `-Wall -Wextra -Werror -std=c++98 -fsanitize=address`, без предупреждений.

---

Назад к оглавлению: [`README.md`](README.md).
