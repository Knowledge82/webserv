# webserv — учебный разбор (архитектура и flow)

README написан как **конспект по проекту**: сначала базовые понятия и картина целиком, потом детали модулей, потом расширения.

## 0. Что такое веб‑сервер и зачем он нужен

Веб‑сервер — это программа, которая:

1) **слушает TCP порт** (например `:8080`),
2) принимает подключения от клиентов (браузер, `curl`),
3) читает **HTTP‑запрос** (например `GET / HTTP/1.1`),
4) решает, что делать (отдать файл, выполнить CGI, принять upload),
5) отправляет **HTTP‑ответ** (status line + headers + body),
6) закрывает соединение или оставляет его открытым (keep‑alive).

Сложность не в том, чтобы “склеить строку ответа”, а в том, чтобы:
- не зависать на одном клиенте,
- обслуживать много соединений параллельно,
- корректно переживать обрывы соединений,
- выдавать правильные HTTP‑статусы.

Проект 42 `webserv` как раз про это.

## 0.1 Какие бывают веб‑серверы и почему мы ориентируемся на NGINX

Исторически популярные веб‑серверы (и что у них “в голове”):

- **Apache HTTP Server** — классический сервер “старой школы”. Исторически часто использовал модель “процесс/поток на соединение” (сейчас есть разные MPM), много возможностей через модули, конфиг довольно богатый. Хорош для совместимости и сложных сценариев, но архитектурно тяжелее.

- **NGINX** — современный “event‑driven” сервер: один (или несколько) event loop’ов обслуживают много соединений неблокирующим образом. Это очень близко к тому, что требует subject `webserv`: один `poll()/epoll()/kqueue` и никакой блокировки на клиентах.

- **Lighttpd** — тоже лёгкий event‑driven сервер, похож по философии.

- **Caddy** — современный сервер с очень удобной конфигурацией (Caddyfile), часто используется с автоматическим HTTPS. По архитектуре тоже событийный, но конфиг не “nginx‑like”.

В subject `webserv` прямо предлагается ориентироваться на NGINX при сравнении поведения ответов и заголовков. Кроме того, формат конфигурации “server/location + директивы” мы тоже берём в стиле NGINX (с сильным упрощением).

---

## 0.2 Что значит “nginx‑like конфиг”: структура и базовые понятия

**nginx‑like конфиг** — это текстовый файл, который состоит из:

1) **блоков** в фигурных скобках `{ ... }`,
2) **директив** (команд), которые заканчиваются точкой с запятой `;`,
3) (обычно) комментариев `# ...` до конца строки.

### 0.2.1 Директива
Директива выглядит так:

```nginx
name arg1 arg2 ... ;
```

Примеры:
```nginx
listen 0.0.0.0:8080;
root /var/www/site;
index index.html;
```

- `name` — имя директивы
- `arg1 arg2 ...` — аргументы (0 или больше)
- `;` — обязательный конец директивы

### 0.2.2 Контексты (где что разрешено)
В nginx‑like конфиге команды зависят от того, **в каком контексте** ты находишься:

- **top‑level (верхний уровень)** — вне любых `{}`  
  В моём проекте на верхнем уровне разрешены только `server { ... }` блоки.

- **server‑контекст** — внутри `server { ... }`  
  Здесь задаются настройки “сайта/виртуального сервера”: где слушать (`listen`), базовый root, error pages и т.п.

- **location‑контекст** — внутри `location <prefix> { ... }`  
  Это настройки для конкретного URL‑префикса: какие методы разрешены, нужен ли autoindex, где хранить upload и т.п.

### 0.2.3 Общая структура файла (упрощённо)
Типичный скелет:

```nginx
server {
  listen 0.0.0.0:8080;

  # server directives
  root /var/www/site;
  index index.html;

  # location blocks
  location / {
    # location directives
    allow_methods GET;
  }

  location /upload {
    allow_methods POST;
    upload_dir /tmp/uploads;
  }
}
```

Важная идея: **server задаёт “глобальные” правила**, а `location` задаёт **правила для конкретных путей** (URI‑префиксов).

### 0.2.4 Что такое “prefix match” в location
`location /upload { ... }` обычно означает:
- все запросы с URI, начинающимся на `/upload` (например `/upload`, `/upload/file.txt`) попадают под этот блок.

(В NGINX есть более сложные виды location, включая regex, но в subject `webserv` regex не требуется.)

### 0.2.5 Почему это удобно
Такой конфиг позволяет описывать сайт как набор правил:
- “на каких адресах/портах слушать”
- “где лежат файлы”
- “какие методы разрешены на маршруте”
- “куда класть загруженные файлы”
- “когда делать редирект”
- “какие error pages отдавать”

И именно этот стиль конфигурации мы реализуем в проекте.

---

## 1. Требования subject (Requirements) — разбор построчно

### 1) Your program must use a configuration file...
**О чём речь:** сервер должен уметь стартовать с конфигом из аргумента или иметь конфиг по умолчанию.  
- сделано: `./webserv [config_file]`, загрузка через `ConfigLoader::loadFromFile()`.  
- сделано: без аргументов используется `ConfigLoader::loadDefault()` (дефолтный конфиг).

---

### 2) You cannot execve another web server.
**О чём речь:** нельзя “обмануть” проект запуском nginx/apache вместо своего.  
- сделано: в проекте нет `execve()` для запуска веб‑сервера.  
- план: `execve()` будет использоваться только для CGI (и только если CGI включён в конфиг).

---

### 3) Your server must remain non-blocking at all times and properly handle client disconnections...
**О чём речь:** нельзя зависать на I/O; клиент может отвалиться в любой момент, сервер не должен падать.  
- сделано: listening sockets и client sockets переводятся в `O_NONBLOCK` (через `fcntl`).
## `fcntl` и `O_NONBLOCK`

### `fcntl`

`fcntl` (file control) — системный вызов POSIX для управления параметрами файловых дескрипторов. Позволяет читать и изменять флаги уже открытого fd без его пересоздания.

```c
#include <fcntl.h>
int fcntl(int fd, int cmd, ... /* arg */);
```

Две команды, используемые в проекте:
- `F_GETFL` — получить текущие флаги fd
- `F_SETFL` — установить новые флаги fd

---

### `O_NONBLOCK`

Флаг режима работы файлового дескриптора. По умолчанию все сокеты **блокирующие** — вызовы `accept()`, `recv()`, `send()` приостанавливают процесс до завершения операции.

С флагом `O_NONBLOCK` эти вызовы возвращаются **немедленно**: если операция не может быть выполнена прямо сейчас — возвращают `-1` с `errno = EAGAIN` вместо того чтобы ждать.

---

### Использование в webserv

```c
static void setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        throw std::runtime_error("fcntl(F_GETFL) failed");
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw std::runtime_error("fcntl(F_SETFL) failed");
}
```

Сначала читаем существующие флаги через `F_GETFL`, затем добавляем `O_NONBLOCK` через побитовое ИЛИ и записываем обратно через `F_SETFL`. Побитовое ИЛИ обязательно — без него перезапишешь другие уже установленные флаги.

Устанавливается на все сокеты сразу после создания:
- listen-сокет — после `socket()`
- клиентские сокеты — после `accept()`

В связке с `poll()` это фундамент неблокирующего I/O: `poll()` сигнализирует когда fd готов, после чего `recv()` / `send()` гарантированно не заблокируются.

---
 
- сделано: обрывы обрабатываются: `recv()==0`/ошибка → соединение закрывается; `POLLHUP/POLLERR` → закрываем fd.  
- план: добавить таймауты, чтобы “медленные” клиенты не могли держать соединение бесконечно.

---

### 4) Use only 1 poll() for all I/O operations (listen included).
**О чём речь:** один общий event loop, один `poll()` на всё: accept/recv/send + будущие pipes CGI.  
**Как сделано/сделаю:**  
- сделано: `Server::run()` делает **один** `poll()` на массиве `pollFds_`, который включает и listen fd, и client fd.  
- план: CGI pipes тоже будут добавляться в тот же `pollFds_` (иначе 0 баллов).

---

### 5) poll() must monitor both reading and writing simultaneously.
**О чём речь:** в одном `poll()` должны отслеживаться события чтения и записи.  
**Как сделано/сделаю:**  
- сделано: для listen fd всегда `POLLIN`.  
- сделано: для клиентов `Connection::wantedPollEvents()` включает `POLLIN` в READING и `POLLOUT` в WRITING (когда есть `out_`).  
- сделано: один `poll()` возвращает готовность и на чтение, и на запись для разных fd.

---

### 6) Never do read/write without going through poll().
**О чём речь:** нельзя вызывать `recv/send/accept/read/write` “наугад”; только после готовности от `poll()`.  
**Как сделано/сделаю:**  
- сделано: `accept()` вызывается только если `poll()` дал `POLLIN` на listen fd.  
- сделано: `recv()` вызывается только если `poll()` дал `POLLIN` на client fd.  
- сделано: `send()` вызывается только если `poll()` дал `POLLOUT` на client fd.  
- план: для CGI `read/write` по pipe тоже только после готовности от `poll()`.

---

### 7) Checking errno is strictly forbidden after read/write.
**О чём речь:** нельзя смотреть `errno` и менять поведение (например “если EAGAIN — одно, если EINTR — другое”).  
**Как сделано/сделаю:**  
- сделано: после `accept/recv/send` код не анализирует `errno`. Любая ошибка → прекращаем текущую операцию и возвращаемся к `poll()`.  
- план: придерживаться этого правила и для CGI I/O.

---

### 8) Disk files are exempt (no poll required).
**О чём речь:** файлы на диске можно читать/писать обычным `read/write` без readiness от `poll()`.  
**Как сделано/сделаю:**  
- план: для статики (GET файлов) и upload (POST запись на диск) будем использовать обычный файловый I/O.  
- важно: “I/O that can wait” (сокеты/пайпы) — только через `poll()`.

---

### 9) I/O that can wait must be non-blocking and driven by a single poll()...
**О чём речь:** повторение и уточнение: сокеты/пайпы/FIFO — nonblocking + единственный poll + никакого recv/send без готовности.  
**Как сделано/сделаю:**  
- сделано: сокеты nonblocking + poll gating.  
- план: CGI pipes nonblocking + poll gating.

---

### 10) You can use every associated macro/helper with poll/select.
**О чём речь:** разрешено использовать удобные макросы/утилиты вокруг выбранного API.  
**Как сделано/сделаю:**  
- сделано: используем `poll` + флаги `POLLIN/POLLOUT/POLLERR/...` напрямую.  
- (для `select` были бы FD_SET и т.п., но мы на poll.)

---

### 11) A request should never hang indefinitely.
**О чём речь:** клиент не должен “повесить” сервер навсегда (slowloris, зависшие CGI, вечное ожидание body).  
**Как сделано/сделаю:**  
- частично сделано: `poll()` с таймаутом не даёт серверу зависнуть целиком, но отдельный клиент может висеть бесконечно.  
- план: добавить таймауты на соединение:
  - таймаут чтения заголовков,
  - таймаут чтения body,
  - таймаут CGI,
  - (позже) keep-alive idle timeout.

---

### 12) Compatible with standard web browsers.
**О чём речь:** ответы и поведение должны быть достаточно правильными для Chrome/Firefox/и т.п.  
**Как сделано/сделаю:**  
- частично сделано: формируем валидные HTTP‑ответы с `Content-Length` и CRLF.  
- план: реализация статики, корректные Content-Type, корректные статусы/ошибки, возможно keep-alive.

---

### 13) NGINX may be used to compare headers and behaviours.
**О чём речь:** можно сравнивать ответы с nginx (какие статусы, какие заголовки).  
**Как сделано/сделаю:**  
- план: использовать nginx как “эталон поведения” в спорных случаях (особенно для ошибок, редиректов, directory index, CGI).

---

### 14) HTTP response status codes must be accurate.
**О чём речь:** нельзя отвечать “200” на ошибки; нужны правильные 404/405/413/500 и т.п.  
**Как сделано/сделаю:**  
- частично сделано: `HttpRequest` выдаёт 400/413/431 при ошибках парсинга.  
- план: добавить статус‑коды для файловой логики:
  - 200/201/204,
  - 301/302 (return directive),
  - 403/404/405,
  - 500 и др.

---

### 15) Default error pages if none are provided.
**О чём речь:** если в конфиге нет `error_page`, сервер всё равно обязан уметь ответить понятной страницей ошибки.  
**Как сделано/сделаю:**  
- сделано: `HttpResponse::buildErrorResponse(status)` генерирует простое тело ошибки (text/plain).  
- план: если `error_page` задан — пытаться отдать файл‑страницу ошибки, иначе использовать default.

---

### 16) You can’t use fork for anything other than CGI.
**О чём речь:** нельзя форкать “для многопоточности/многопроцессности” — только для CGI.  
**Как сделано/сделаю:**  
- сделано: fork сейчас не используется.  
- план: fork появится только в подсистеме CGI и только по конфигу.

---

### 17) Serve a fully static website.
**О чём речь:** GET должен отдавать реальные файлы/директории из root (с index/autoindex).  
**Как сделано/сделаю:**  
- план: реализовать mapping `URI -> filesystem path` через `root` (server/location), `index`, `autoindex`, `error_page`, `allow_methods`.

---

### 18) Clients must be able to upload files.
**О чём речь:** POST должен позволять сохранить тело запроса в файл (в upload_dir).  
**Как сделано/сделаю:**  
- план: директивы `upload_dir` уже парсятся, далее будет реализация:
  - проверка allowed methods,
  - проверка client_max_body_size,
  - запись body на диск,
  - возврат корректного статуса.

---

### 19) Need at least GET, POST, DELETE.
**О чём речь:** обязательные методы.  
**Как сделано/сделаю:**  
- план: реализовать обработчики GET/POST/DELETE с правильными статусами и политиками access.

---

### 20) Stress test to ensure it remains available at all times.
**О чём речь:** под нагрузкой не должен падать/зависать/утекать память/ломать poll-loop.  
**Как сделано/сделаю:**  
- план: нагрузочные тесты (например `wrk`, `ab`, `siege`), плюс тесты “медленный клиент”, плюс valgrind/sanitizers.  
- план: таймауты и аккуратное закрытие соединений.

---

### 21) Listen to multiple ports to deliver different content.
**О чём речь:** сервер должен слушать несколько портов (и потенциально отдавать разные сайты/настройки).  
**Как сделано/сделаю:**  
- сделано: конфиг поддерживает несколько `listen` директив, `Server` поднимает несколько listening sockets (`listenFds_`).  
- план: добавить привязку `listenFd -> ServerConfig`, чтобы разные порты реально соответствовали разным `server` блокам/контенту.

---

### 22) Virtual hosts out of scope (allowed if you want).
**О чём речь:** выбирать server по `Host:` заголовку не обязательно.  
**Как сделано/сделаю:**  
- план: сначала реализовать обязательное (разные порты = разные server).  
- опционально: позже можно добавить `server_name` и выбор по Host header.

---

## Что делает мой `webserv` (текущий этап)

Сервер уже умеет:
- читать конфигурацию nginx‑like (`server { ... }`, `location ... { ... }`),
- поднимать **несколько listening sockets** по директивам `listen`,
- работать полностью неблокирующе и использовать **один `poll()`** на все I/O (listen + clients),
- инкрементально разбирать HTTP‑запрос (частями),
- отвечать базовым ответом или корректными ошибками парсинга HTTP (400/413/431).

Дальше по плану: статика, upload, DELETE, CGI, таймауты.

---

## 2. Самый простой конфиг и симуляция работы

### 2.1 Самый простой конфиг
Файл `minimal.conf`:

```nginx
server {
  listen 127.0.0.1:8080;
}
```

Что это значит:
- поднять сервер и слушать порт 8080 **только на локальной машине** (loopback).

### 2.2 Запуск
```bash
./webserv minimal.conf
```

### 2.3 Flow по шагам (что реально происходит в программе)

#### Шаг A — загрузка конфига (main → ConfigLoader → ConfigParser)
1) `main()` вызывает `ConfigLoader::loadFromFile("minimal.conf")`.
2) `ConfigLoader` создаёт `ConfigParser`.
3) `ConfigParser` через `Tokenizer` читает токены из файла.
4) `ConfigParser::parseConfig()` строит структуру `Config`:
   - `cfg.servers.size() == 1`
   - `cfg.servers[0].listens.size() == 1`
   - listen = `127.0.0.1:8080`

Если конфиг кривой — выбрасывается исключение `std::runtime_error` с `line/col`, `main` печатает `Fatal: ...` и завершает работу.

#### Шаг B — поднятие listening socket (Server::setupListenSockets)
1) `main()` создаёт `Server s(cfg)`.
2) В конструкторе `Server` вызывается `setupListenSockets()`.
3) Создаётся listening socket:
   - `socket()`
   - `setsockopt(SO_REUSEADDR)`
   - `fcntl(O_NONBLOCK)`
   - `bind(127.0.0.1:8080)`
   - `listen()`
4) fd кладётся в `listenFds_`.

#### Шаг C — основной event loop (Server::run)
`Server::run()` крутится в бесконечном цикле:
1) пересобирает `pollFds_` (listen fd + client fds),
2) вызывает `poll()`,
3) если есть события — обрабатывает их.

#### Шаг D — клиент подключился
Допустим ты сделал:
```bash
curl -v http://127.0.0.1:8080/
```

1) `poll()` сообщает `POLLIN` на listen fd.
2) `Server::acceptPendingConnections(listenFd)` вызывает `accept()` и получает `clientFd`.
3) Создаётся `Connection(clientFd)` и кладётся в `connections_`.

#### Шаг E — клиент отправил HTTP запрос
1) `poll()` сообщает `POLLIN` на `clientFd`.
2) `Connection::onReadable()` вызывает `recv()` и дописывает байты в `in_`.
3) `HttpRequest::parse(in_, ...)` пытается распарсить запрос:
   - если заголовки ещё не полностью пришли → `HEADERS`
   - если body не полностью пришло → `BODY`
   - если запрос готов → `COMPLETE`
   - если запрос невалидный → `ERROR` + статус (400/413/431)

#### Шаг F — сервер отвечает
1) Когда request стал `COMPLETE`, Connection готовит строку ответа в `out_` и переходит в `WRITING`.
2) `poll()` сообщает `POLLOUT` на `clientFd`.
3) `Connection::onWritable()` делает `send()` (частями, если надо), удаляя отправленное из `out_`.
4) Когда `out_` пуст — соединение закрывается (в ответе `Connection: close`).

---

## 3. Сборка и запуск

### 3.1 Сборка
```bash
make
```

### 3.2 Запуск
```bash
./webserv [config_file]
```

Если `config_file` не передан — используется конфиг по умолчанию (`ConfigLoader::loadDefault()`), который поднимает `0.0.0.0:8080`.

### 3.3 Проверка конфигурации без запуска сервера
```bash
./webserv --check-config [config_file]
```

- Без `config_file` проверяется конфиг по умолчанию.
- При успехе печатается `OK: ...`, код выхода `0`.
- При ошибке печатается `Fatal: ...`, код выхода `1`.

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
