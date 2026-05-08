# webserv — архитектура и flow (42)

## 0. Цель проекта
Этот проект — учебная реализация минимального HTTP-сервера на C++98.
Основная цель текущего этапа: неблокирующий сервер на `poll()`, поддержка нескольких listen-сокетов, инкрементальный разбор HTTP-запроса и базовая генерация ответа.

## 1. Сборка и запуск

### 1.1 Сборка
```bash
make
```

### 1.2 Запуск
```bash
./webserv [config_file]
```

Если `config_file` не передан — используется конфиг по умолчанию (`ConfigLoader::loadDefault()`).

### 1.3 Проверка конфигурации без запуска сервера
```bash
./webserv --check-config [config_file]
```

- Без `config_file` проверяется конфиг по умолчанию.
- При успехе печатается `OK: ...`, код выхода `0`.
- При ошибке: `Fatal: ...`, код выхода `1`.

## 2. Карта модулей (ответственности)

### 2.1 Config / ConfigLoader / Tokenizer / Parser
**Config.hpp** содержит только структуры данных конфигурации.

- `Config` содержит список `servers`.
- `ServerConfig` содержит:
  - `listens` (`host:port`)
  - `root`, `index`, `client_max_body_size`
  - `errorPages`
  - `locations`
- `LocationConfig` содержит настройки для URI-префикса (`prefix`) и флаги `hasX` для наследования server → location.

**ConfigLoader** — фасад:
- `loadFromFile(path)` → читает конфиг из файла
- `loadDefault()` → возвращает минимальный конфиг по умолчанию

**Tokenizer** — лексер:
превращает поток символов в токены: WORD, `{`, `}`, `;`, EOF.

**ConfigParser** — LL(1) парсер с одним lookahead токеном (`nextToken_`):
- top-level: только блоки `server { ... }`
- внутри server: директивы + `location <prefix> { ... }`
- директивы применяются отдельными функциями:
  - `applyServerDirective(...)`
  - `applyLocationDirective(...)`

Ограничения языка конфига:
- нет кавычек
- нет escape
- комментарии `# ... \n`

(Поддерживаемые директивы будут перечислены позже, когда зафиксируем реализацию в .cpp.)

### 2.2 Server — event loop и poll()
`Server` отвечает за:
- поднятие всех listening sockets по конфигу
- мультиплексирование событий через `poll()`
- accept новых клиентов
- управление набором соединений `fd -> Connection`

### 2.3 Connection — одно TCP-соединение
`Connection` отвечает за:
- чтение из сокета в `in_`
- инкрементальный парсинг HTTP через `HttpRequest`
- подготовку ответа в `out_`
- отправку ответа (partial send) и закрытие соединения

`Server` не знает деталей HTTP, он только вызывает `onReadable()` / `onWritable()`.

### 2.4 HttpRequest — инкрементальный разбор HTTP
`HttpRequest::parse()` потребляет байты из входного буфера и переходит по состояниям:
- HEADERS → BODY → COMPLETE
- или ERROR (с `errorStatus_`)

Парсер поддерживает лимиты:
- `maxHeaderBytes`
- `maxBodyBytes` (Content-Length)

### 2.5 HttpResponse — генерация ответа
Минимальный набор builder-функций:
- `buildHelloResponse()`
- `buildErrorResponse(status)`

## 3. Flow обработки (энд-ту-энд)
TODO: заполнить по фактическому коду `.cpp`:
- создание listen сокетов
- poll loop
- accept
- recv -> in_
- request.parse(...)
- build response -> out_
- send -> closing

## 4. Модель ошибок и ограничений
TODO: заполнить после просмотра `.cpp`:
- какие коды возвращаются при parse error
- что считается fatal
- policy по errno (42 rule)

## 5. Текущее состояние и roadmap
- [x] multi-listen sockets
- [x] LL(1) config parser (nginx-like)
- [ ] listenFd -> serverIndex mapping
- [ ] client_max_body_size -> HttpRequest::parse(maxBodyBytes)
- [ ] location selection by URI + 404/405/413
- [ ] upload_dir
- [ ] keep-alive
- [ ] chunked/multipart (не планируется на MVP)
