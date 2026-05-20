### 19.05.26

## `Connection::onReadable()` — разбор по слоям (recv → parse → route → fs → response)

Эта функция делает один “тик” обработки соединения, когда `poll()` сказал: **есть данные на чтение (POLLIN)**.

У тебя архитектура простая и рабочая:
- `onReadable()` читает вход, парсит HTTP, и если запрос готов — **готовит ответ в `out_`** и переключает `state_ = WRITING`.
- `onWritable()` потом отправляет `out_` через `send()` (частями) и закрывает соединение (пока `Connection: close`).

---

# 0) Состояния Connection и буферы
- `in_` — накопитель входящих байт (recv может приносить кусками)
- `out_` — накопитель исходящих байт (send может отправлять частично)
- `request_` — incremental HTTP parser со state machine:
  - `HEADERS → BODY → COMPLETE` или `ERROR`

---

# 1) Layer 1: Network read (recv)
Код:
```cpp
ssize_t n = recv(fd_, buf, sizeof(buf), 0);
```

Возможные исходы:
- `n == 0` → клиент закрыл соединение → `return false` (Server закроет fd)
- `n < 0` → ошибка → `return false`
- иначе → добавляем в `in_`:
  - `in_.append(buf, n);`

Идея: даже если пришло мало, мы **копим** в `in_`, пока не будет достаточно для парсера.

---

# 2) Layer 2: HTTP parse (incremental)
Код:
```cpp
HttpRequest::State st = request_.parse(in_, maxHeaderBytes, maxBodyBytes);
```

Важно: `parse()` **потребляет** байты из `in_`:
- нашёл `\r\n\r\n` → вырезал заголовки из `in_`
- если есть body → вырезал body из `in_`
- в `in_` может остаться “хвост” (в будущем для keep-alive/pipelining)

Состояния:
- `HEADERS` → ждём ещё байт → `onReadable()` просто `return true`
- `BODY` → ждём ещё body → `return true`
- `ERROR` → готовим ошибку `400/413/431`, кладём в `out_`, `state_ = WRITING`
- `COMPLETE` → запрос готов → начинаем обработку роутинга и файлов

---

# 3) Layer 3: Роутинг и политики (server/location → EffectiveConfig)
Когда `COMPLETE`, мы:
1) проверяем конфиг и `serverIndex_`
2) выбираем `location` по **longest prefix match**
3) делаем merge настроек:
   - server defaults
   - location overrides

Результат: `EffectiveConfig eff`, где уже “готовые” параметры:
- `root`, `index`, `autoindex`, `allow_methods`, `return ...`

Сразу применяем политики:
- `return` (redirect) имеет приоритет
- `allow_methods` / метод != GET → 405
- root должен быть задан → иначе 500

---

# 4) Layer 4: URI → filesystem path (safeJoin)
Это самый важный кусок безопасности.

### Почему не `containsDotDot()` и не `joinPath(root, uri.substr(1))`?
Потому что:
- URI может содержать `%2e%2e` (encoded `..`)
- бывают нормальные пути с `..` внутри root (`/a/b/../c`)
- нужно нормализовать сегменты и запретить выход выше root

### Что делает `safeJoin(root, rawUri)`:
Политика:
- `?query` отрезаем
- `#fragment` запрещаем → 400 (строгая политика)
- делаем percent-decode
- запрещаем `%2F` (encoded slash) → 400
- нормализуем `.` и `..`
- если `..` пытается выйти выше root → 403

Результат:
- либо получаем безопасный `path` в FS
- либо сразу отдаём `400/403`

---

# 5) Layer 5: Файловая логика через `stat()` (PathKind/classifyPath)
Теперь мы не делаем “readFileToString упал → 404”.
Сначала определяем “что это за path”:

`PathKind pk = classifyPath(path)` возвращает:
- `PATH_FILE` — это файл
- `PATH_DIR` — это директория
- `PATH_MISSING` — не существует → 404
- `PATH_FORBIDDEN` — нет прав → 403
- `PATH_ERROR` — непонятная ошибка FS → 500

---

# 6) Directory flow: redirect → index → autoindex → 403
Если `pk == PATH_DIR`:

### 6.1 Redirect `/dir` → `/dir/`
Если URI не оканчивается `/`, отдаём 301.
Это нужно, чтобы autoindex и относительные ссылки работали корректно.

### 6.2 Index
Если `eff.hasIndex`:
- `indexPath = joinPath(dirPath, eff.index)`
- проверяем `classifyPath(indexPath)`:
  - `PATH_FILE` → читаем и отдаём index
  - `PATH_FORBIDDEN` → 403 (и НЕ autoindex)
  - `PATH_ERROR` → 500
  - `PATH_MISSING` → index нет → возможно autoindex

### 6.3 Autoindex
Если index не отдали:
- если `autoindex on` → генерим HTML listing
- иначе → 403

---

# 7) File flow: читаем и отдаём
Если `pk == PATH_FILE`:
- читаем `readFileToString(path, body)`
- если чтение внезапно упало, хотя stat говорил “файл есть” → 500
- иначе отдаём:
  - `200 OK`
  - `Content-Type` по расширению
  - body

---

# Flow на примерах

## Пример A: `GET /docs` где `/docs` — директория
1) `safeJoin` → `path = ./www/docs`
2) `classifyPath(path)` → `PATH_DIR`
3) URI без `/` → `301 Location: /docs/`
4) дальше клиент делает новый запрос на `/docs/`

## Пример B: `GET /docs/` директория без index, autoindex on
1) `safeJoin` → `./www/docs`
2) `PATH_DIR`
3) URI уже со `/` → редирект не нужен
4) index:
   - `./www/docs/index.html` → `PATH_MISSING`
5) autoindex on → отдаём HTML listing

## Пример C: `GET /docs/` и `index.html` существует, но EACCES
1) директория `PATH_DIR`
2) indexPath `PATH_FORBIDDEN`
3) отдаём **403**, НЕ autoindex
   (иначе это выглядело бы как “обход запрета”: index закрыт, но получай список файлов)

## Пример D: атака `GET /../secret`
1) `safeJoin` видит, что `..` пытается выйти выше root
2) сразу 403, до любых stat/read

## Пример E: `GET /a%2Fb.txt`
1) decode даёт `'/'` из `%2F`
2) политика: encoded slash forbidden → 400

---

# Что важно держать в голове
- `safeJoin` отвечает за безопасность URI и нормализацию.
- `classifyPath(stat)` отвечает за корректный HTTP статус (403/404/500) ещё ДО чтения.
- `readFileToString` теперь не решает “что отвечать”, он только “прочитать/не прочитать”.
