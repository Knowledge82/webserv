# webserv — архитектура и flow (42)

## 0. Цель проекта
Этот репозиторий — моя учебная реализация веб-сервера на C++98 (проект 42 `webserv`).

README — это **учебное пособие по текущему коду**:
- что за модуль за что отвечает
- как данные/события текут по программе
- какие есть инварианты (что всегда должно быть истинно)
- какие ошибки/ограничения есть сейчас и почему

Цель не “сделать по-быстрому”, а построить сервер так, чтобы я:
1) понимал архитектуру,
2) мог расширять её без переписывания всего,
3) мог отлаживать баги по логике, а не методом тыка.

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

## Конфигурация

### Общая идея
Конфиг написан в nginx-like стиле: блоки `server { ... }` и вложенные `location <prefix> { ... }`.
Директива — это команда вида:

```
name arg1 arg2 ... ;
```

### Лексика
Tokenizer выделяет токены:
- `WORD` — строка до пробела или символов `{ } ; #`
- `{`, `}`, `;`
- `EOF`

Комментарии начинаются с `#` и продолжаются до конца строки.

Ограничения:
- кавычки не поддерживаются
- escape-последовательности не поддерживаются

### Грамматика (упрощённо)
На верхнем уровне разрешены только `server`-блоки:

```
server { ... }
server { ... }
```

Внутри `server`:
- директивы server-контекста
- `location <prefix> { ... }`

### Поддерживаемые директивы server-контекста
- `listen host:port;`
- `root <path>;`
- `index <filename>;`
- `client_max_body_size <bytes>;`
- `error_page <code> <path>;`

Если директив `listen` нет, добавляется default: `0.0.0.0:8080`.

### Поддерживаемые директивы location-контекста
- `root <path>;`
- `index <filename>;`
- `autoindex on|off;`
- `allow_methods M1 M2 ...;`
- `upload_dir <path>;`
- `return <code> <target>;`

### Наследование server → location
Структуры содержат пары `hasX + X`. Если в location директива не задана (`hasX == false`), значение должно наследоваться от server-конфига. (Механизм применения “effective config” будет описан после реализации выбора location по URI.)

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

Ниже описан реальный runtime-flow по текущему коду (main → Server → Connection → HttpRequest/HttpResponse).

### 3.1 Запуск программы (main)
1) `main()` выбирает конфигурацию:
   - `./webserv` → `ConfigLoader::loadDefault()`
   - `./webserv <file>` → `ConfigLoader::loadFromFile(file)`
   - `./webserv --check-config [file]` → только парсит конфиг и завершает работу (без запуска сервера)

2) При успешной загрузке конфигурации создаётся:
   - `Server s(cfg);`
   - `s.run();`

Если в процессе загрузки конфигурации случилась ошибка (например ошибка парсинга) — бросается исключение, `main` печатает `Fatal: ...` и завершает работу.

---

### 3.2 Поднятие слушающих сокетов (Server::setupListenSockets)
`Server` хранит:
- `listenFds_`: список всех listening sockets
- `connections_`: map `clientFd -> Connection`

При создании `Server` вызывается `setupListenSockets()`.

Алгоритм:
1) пройти по `cfg_.servers`
2) в каждом `ServerConfig` пройти по `listens`
3) для каждого `listen host:port` создать сокет:

- `socket(AF_INET, SOCK_STREAM, 0)`
- `setsockopt(SO_REUSEADDR)`
- `fcntl(O_NONBLOCK)` (через `setNonBlocking`)
- заполнить `sockaddr_in` (host/port)
- `inet_pton(host)`
- `bind()`
- `listen(backlog=128)`
- сохранить fd в `listenFds_`

Каждый успешный listen fd логируется:
`Listening on <host>:<port> (fd=<n>)`.

Важно:
- это “multi-listen”: в `listenFds_` может быть несколько fd (например разные порты).

---

### 3.3 Основной event loop (Server::run)
`Server::run()` — бесконечный цикл.

На каждой итерации:
1) `buildPollFds()` пересобирает `pollFds_`:
   - сначала добавляет все `listenFds_` с `events = POLLIN`
   - затем добавляет все `clientFd` из `connections_` с `events = Connection::wantedPollEvents()`

2) вызывается `poll(&pollFds_[0], pollFds_.size(), 1000)`

3) если на любом listen fd есть `POLLIN`, выполняется:
   - `acceptPendingConnections(listenFd)`

4) затем обрабатываются клиентские fd:
   - ошибки `POLLERR|POLLHUP|POLLNVAL` → `closeConnection(fd)`
   - `POLLIN` при `Connection::READING` → `Connection::onReadable()`
   - `POLLOUT` при `Connection::WRITING` → `Connection::onWritable()`
   - если `onReadable/onWritable` возвращает `false`, сервер закрывает соединение

---

### 3.4 Принятие новых подключений (acceptPendingConnections)
`acceptPendingConnections(listenFd)` вызывает `accept()` в цикле:
- пока `accept()` возвращает валидный client fd — добавляет Connection
- как только `accept()` возвращает `< 0` — прекращает цикл

Важно: по правилам проекта после I/O мы **не анализируем errno**.
Мы не различаем `EAGAIN`, `EINTR`, и т.п. — просто выходим, и `poll()` разбудит снова.

Для каждого клиента:
- `setNonBlocking(clientFd)`
- `connections_[clientFd] = Connection(clientFd)`

---

### 3.5 Чтение данных и HTTP-парсинг (Connection::onReadable + HttpRequest::parse)
`Connection::onReadable()`:
1) делает `recv()` в буфер (4096 байт)
2) если `recv()` вернул:
   - `0` → клиент закрыл соединение → вернуть `false`
   - `< 0` → ошибка чтения → вернуть `false`
3) дописывает байты в `in_`
4) вызывает инкрементальный парсер:

```cpp
HttpRequest::State st = request_.parse(in_, maxHeaderBytes, maxBodyBytes);
```

Ключевая идея: `parse()` **потребляет байты из `in_`**:
- распарсил заголовки → удалил их из `in_`
- распарсил body → удалил body из `in_`
То есть `in_` одновременно и “накопитель”, и “очередь необработанных байт”.

Состояния:
- `HEADERS`: заголовки ещё не полностью получены → Connection остаётся в `READING`
- `BODY`: заголовки распарсены, но body ещё не полностью пришло → остаёмся в `READING`
- `COMPLETE`: запрос готов → строим ответ, переходим в `WRITING`
- `ERROR`: запрос невалидный → строим ошибку, переходим в `WRITING`

---

### 3.6 Запись ответа (Connection::onWritable)
`Connection::onWritable()`:
1) делает `send()` части `out_`
2) удаляет отправленную часть из `out_`
3) когда `out_` становится пустым, соединение закрывается

Почему закрывается:
- текущая версия ответов включает `Connection: close`
- поэтому после ответа соединение не переиспользуется (keep-alive пока не реализован)

Важно: `out_` нужен, потому что `send()` может отправить только часть данных (partial send).

## 4. Модель ошибок и ограничения (по текущему коду)

### 4.1 Ошибки конфигурации
Ошибки парсинга конфига оформлены как исключения `std::runtime_error` с указанием позиции:
`config parse error at line X, col Y: ...`

Такие ошибки считаются **фатальными**:
- конфиг не загружен → сервер не стартует → `main` печатает `Fatal: ...` и завершает работу с кодом 1.

---

### 4.2 Ошибки сокетов при старте сервера
Ошибки `socket/bind/listen/inet_pton/fcntl` при создании listening sockets тоже приводят к исключению и завершению запуска.
Причина: сервер не может корректно работать, если не поднял слушающие сокеты.

---

### 4.3 Ошибки I/O во время работы сервера (клиенты)
Ошибки клиентских операций считаются **локальными** для соединения:
- `recv() == 0` → клиент закрыл соединение → закрываем client fd
- `recv() < 0` → ошибка чтения → закрываем client fd
- `send() <= 0` → ошибка записи → закрываем client fd
- события `POLLERR | POLLHUP | POLLNVAL` → закрываем client fd

Сервер продолжает работать и обслуживать другие соединения.

---

### 4.4 “Не смотреть errno” (правило проекта)
После операций `accept/recv/send` код не анализирует `errno`.
Политика:
- если системный вызов вернул ошибку, соединение/операция прекращается, сервер возвращается в `poll()` и ждёт новых событий.

Это упрощает обработку и соответствует требованиям проекта (но может быть расширено позже, если понадобится более тонкая диагностика).

---

### 4.5 HTTP ограничения (HttpRequest)
Поддерживаемый поднабор:
- только `Content-Length` для body
- chunked transfer encoding не поддерживается
- keep-alive пока не реализован

Лимиты (сейчас заданы в `Connection::onReadable` как константы):
- `maxHeaderBytes = 16KB`:
  - если `\r\n\r\n` не найден, а буфер превысил лимит → ошибка 431
- `maxBodyBytes = 1MB`:
  - если `Content-Length > maxBodyBytes` → ошибка 413

Ошибки HTTP-парсинга → состояние `HttpRequest::ERROR` и `errorStatus_`:
- 400: некорректный синтаксис запроса
- 413: слишком большой body по Content-Length
- 431: слишком большие заголовки (header block)

---

### 4.6 Ограничения по конфигу
Конфиг intentionally упрощён:
- нет кавычек в значениях
- нет escape-последовательностей
- top-level содержит только `server { ... }`

## 5. Текущее состояние и roadmap
- [x] multi-listen sockets
- [x] LL(1) config parser (nginx-like)
- [ ] listenFd -> serverIndex mapping
- [ ] client_max_body_size -> HttpRequest::parse(maxBodyBytes)
- [ ] location selection by URI + 404/405/413
- [ ] upload_dir
- [ ] keep-alive
- [ ] chunked/multipart (не планируется на MVP)
