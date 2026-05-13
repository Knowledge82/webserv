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

## Термины (мини‑словарь)

Этот раздел будет расширяться по мере разработки. Цель — чтобы при чтении кода/subject можно было быстро “декодировать” термины.

### HTTP
**HTTP (HyperText Transfer Protocol)** — текстовый протокол “запрос → ответ” поверх TCP.

Пример запроса:
```text
GET / HTTP/1.1
Host: 127.0.0.1:8080

```

Пример ответа:
```text
HTTP/1.1 200 OK
Content-Length: 5
Connection: close

Hello
```

---

### HTTP request / response
- **Request**: request line + headers + пустая строка + (опционально) body.
- **Response**: status line + headers + пустая строка + body.

---

### URI
**URI** (в нашем контексте обычно “path”) — что клиент просит, например `/img/logo.png`.

URI ≠ путь к файлу на диске. Путь на диске строится логикой сервера: `root + URI` (с учётом `location`), плюс проверки безопасности.

---

### Method (GET/POST/DELETE)
- **GET**: получить ресурс (обычно файл).
- **POST**: отправить данные (upload и т.п.).
- **DELETE**: удалить ресурс.

---

### Status code
**HTTP status code** — число, описывающее результат:
- 200 OK
- 201 Created
- 204 No Content
- 301/302 Redirect
- 400 Bad Request
- 403 Forbidden
- 404 Not Found
- 405 Method Not Allowed
- 413 Payload Too Large
- 431 Request Header Fields Too Large
- 500 Internal Server Error

---

### Header / headers block / end-of-headers
- **Header**: строка `Key: Value`.
- **Headers block**: набор заголовков до пустой строки.
- **Конец заголовков** в HTTP/1.x определяется последовательностью: `\r\n\r\n`.

---

### Content-Length
Заголовок `Content-Length` сообщает размер body в байтах.  
Если он есть, сервер должен:
- дождаться ровно этого числа байт body,
- не читать body “на глаз”.

В текущей версии body поддерживается только через `Content-Length` (chunked нет).

---

### chunked transfer encoding (chunked)
Способ передавать body кусками без Content-Length.  
В subject упоминается (особенно для CGI), но **на текущем этапе не поддерживается**. В будущем, если будем делать, придётся “раз‑чанковывать” (un-chunk) до передачи CGI.

---

### keep-alive
**Keep-alive** — когда одно TCP‑соединение используется для нескольких HTTP‑запросов.  
Сейчас ответы содержат `Connection: close`, и соединение закрывается после ответа.

---

### Slowloris
Атака/паттерн: клиент очень медленно присылает заголовки, держит соединение долго и забирает ресурсы.  
Защита: таймауты на чтение заголовков + лимит размера заголовков (у нас уже есть 431 по maxHeaderBytes).

---

## Сеть и сокеты

### TCP
**TCP** — надёжный поток байтов:
- порядок гарантирован,
- границ сообщений нет,
- `recv()` возвращает “сколько дали сейчас”, а не “целый запрос”.

---

### IPv4
**IPv4** — адреса вида `127.0.0.1`, `192.168.1.50`, `0.0.0.0`.

- `127.0.0.1` — loopback (только локальная машина).
- `0.0.0.0` — “все интерфейсы” (когда делаешь bind/listen).

---

### Host/port и bind
- `bind(127.0.0.1:8080)` → доступ только локально.
- `bind(0.0.0.0:8080)` → доступ со всех сетевых интерфейсов (если firewall позволяет).

---

### Socket (сокет) и fd
**Socket** — объект в ядре ОС, `fd` — число‑идентификатор (дескриптор), которым мы этим объектом управляем.

---

### Listening socket (listen socket)
Сокет, который принимает подключения:
- `socket()` → создать
- `bind()` → занять IP:port
- `listen()` → включить режим ожидания подключений

---

### Client socket
Сокет конкретного соединения (результат `accept()`), по нему идут `recv/send`.

---

### bind / listen / accept (коротко)
- `bind()` — “занять адрес/порт”.
- `listen()` — “начать принимать входящие”.
- `accept()` — “получить нового клиента из очереди”.

---

### backlog
Параметр `listen(fd, backlog)` — размер очереди входящих подключений, которые ядро держит, пока приложение не успевает делать `accept()`.

---

### TIME_WAIT
Состояние TCP после закрытия соединения (может держаться ~минуты).  
Из-за TIME_WAIT при перезапуске сервера `bind()` может дать `EADDRINUSE`.

---

### SO_REUSEADDR
Опция сокета, позволяющая быстрее переиспользовать адрес/порт при перезапуске сервера (полезно при TIME_WAIT).

---

### Non-blocking I/O (O_NONBLOCK)
Режим fd, где `accept/recv/send` не блокируют поток выполнения.  
Правильная схема: **сначала `poll()`, потом I/O**.

---

### poll
**poll()** — системный вызов ожидания событий на множестве fd:
- `POLLIN` — можно читать
- `POLLOUT` — можно писать
- `POLLERR/POLLHUP/POLLNVAL` — проблемы/разрыв

Subject требует один общий poll на все I/O (и для CGI pipes тоже).

---

### recv / send (partial read/write)
`recv()` и `send()` могут вернуть меньше, чем ты хотел.  
Поэтому сервер хранит буферы:
- `in_` — накопление входных байт
- `out_` — очередь исходящих байт до полной отправки

---

### Byte order (endianness): little-endian / big-endian
x86 обычно **little-endian**, сеть использует **big-endian** (network byte order).  
Для портов/чисел нужны преобразования:
- `htons` / `ntohs`
- `htonl` / `ntohl`

---

### htons / inet_pton
- `htons(port)` — host→network (16-bit).
- `inet_pton(AF_INET, "1.2.3.4", &addr.sin_addr)` — строковый IPv4 → бинарный.

---

### NAT
**NAT** — когда внешний адрес/порт не равны внутренним (роутер “переводит адреса”).  
Если сервер слушает `0.0.0.0`, то внутри локалки доступ обычно есть, но “из интернета” — только при пробросе портов.

---

### Firewall
Сетевой фильтр (ufw/iptables). Может заблокировать входящие подключения даже если сервер слушает порт.

---

## Конфиг и парсинг

### Directive / block / context
- **Directive**: `name args...;`
- **Block**: `name ... { ... }`
- **Context**: где мы находимся (top-level / server / location). От контекста зависит, какие директивы разрешены.

---

### listen directive
`listen host:port;` — описывает, на каких адресах/портах сервер принимает TCP подключения.  
Несколько listen → несколько listening sockets.

---

### location
`location /prefix { ... }` — правила для URI‑префикса (prefix match).  
Используется, чтобы разные части сайта имели разные политики (methods, root, autoindex, upload, redirect, CGI).

---

### Prefix match (longest prefix match)
Обычно выбирается location с самым длинным подходящим префиксом:
- URI `/upload/file` лучше матчится на `/upload`, чем на `/`.

---

### Inheritance (server → location)
Пары `hasX + X` означают: если в location X не задан, берём X из server.

---

### Token
**Token** — атомарный элемент входа для парсера (WORD, `{`, `}`, `;`, EOF).  
Tokenizer превращает текст конфига в поток токенов.

---

### Lookahead (в парсере)
**Lookahead** — “следующий токен, который мы уже посмотрели, но ещё не обработали”.  
В твоём парсере это `nextToken_`.

Зачем нужен:
- парсер решает, что делать, глядя на текущий токен (например: это `location` или обычная директива?)
- не нужно “читать вперёд и откатываться назад”, что сложно сделать с потоком токенов

Пример в server-блоке:
- если lookahead токен = WORD("location") → парсим location блок
- иначе → парсим директиву `name args...;`

---

## Файлы, безопасность, CGI

### Static files (статика)
“Отдать статический сайт” = отдать файлы с диска по GET:
- построить путь (root + URI),
- проверить существование/права,
- отдать содержимое с корректными headers и status code.

---

### Directory listing (autoindex)
Если URI указывает на директорию:
- если `index` есть и файл существует → отдать index
- иначе если `autoindex on` → сгенерировать listing
- иначе → ошибка (обычно 403 или 404, зависит от политики)

---

### Path traversal (`..`)
Атака/ошибка, когда клиент пытается выбраться из root:
- `/../../etc/passwd`
Сервер должен это запрещать (нормализация пути + проверка, что результат внутри root).

---

### CGI
**CGI** — запуск внешней программы для обработки запроса:
- `fork()` (разрешён только для CGI)
- в child: `dup2()` stdin/stdout, `execve()`
- в parent: общение через pipes
Важно по subject: pipes тоже non-blocking и через тот же `poll()`.

---
### RFC
**RFC (Request for Comments)** — документы, в которых описаны стандарты интернет‑протоколов (HTTP, TCP и т.д.).  
В `webserv` не требуется реализовать весь RFC, но полезно читать, чтобы понимать:
- формат запросов/ответов,
- статусы,
- детали типа `Connection`, `Content-Length`, `Transfer-Encoding`.

---

### MIME / Content-Type
**MIME type** (в HTTP это заголовок `Content-Type`) говорит клиенту, *что именно* в body ответа, например:
- `text/html`
- `text/plain`
- `image/png`
- `application/octet-stream`

Браузер использует `Content-Type`, чтобы понять, как интерпретировать данные (открыть как страницу, картинку, скачать файл).


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

## 2. Простой конфиг и симуляция работы

В этом разделе — детальный flow **main → ConfigLoader → ConfigParser → Tokenizer** на минимальном конфиге, а затем коротко “что будет дальше” (Server/poll), чтобы связать загрузку конфига с запуском сервера.

### 2.1 Простой конфиг
Файл `simple.conf`:

```nginx
server {
        listen 0.0.0.0:8080;
        root /var/www;
        index index.html;
}
```

---

### 2.2 Запуск
```bash
./webserv simple.conf
```

---

### 2.3 Flow: main → ConfigLoader → ConfigParser → Tokenizer (пошагово)

#### Шаг 1 — `main()` выбирает режим и запускает загрузку конфига
Так как мы передали один аргумент и это не `--check-config`, `main()` делает:

- вызывает `ConfigLoader::loadFromFile("simple.conf")`
- получает `Config cfg`
- создаёт `Server s(cfg)` и вызывает `s.run()`

---

#### Шаг 2 — `ConfigLoader::loadFromFile(path)`
`ConfigLoader` — фасад. Он просто делегирует разбор конфигурации:

1) создаёт парсер:
   ```cpp
   ConfigParser p(path);
   ```
2) вызывает:
   ```cpp
   return p.parseConfig();
   ```

---

#### Шаг 3 — создание `ConfigParser` и первый lookahead
Конструктор парсера:

```cpp
ConfigParser::ConfigParser(const std::string &path)
  : tokenizer_(path)
  , nextToken_(tokenizer_.next())
{}
```

То есть происходит два важных действия:

1) создаётся `Tokenizer tokenizer_(path)` (открывает файл и готовит чтение)
2) сразу читается **первый токен** через `tokenizer_.next()` и сохраняется в `nextToken_`

`nextToken_` — это lookahead: “следующий непрочитанный токен”, на который парсер смотрит, чтобы понимать, что делать дальше.

---
#### Шаг 4 — как `Tokenizer` превращает текст в токены (подробно)

`Tokenizer` — это лексический анализатор (лексер). Его задача: взять поток символов из файла и превратить его в поток **токенов** — “атомов синтаксиса”, с которыми уже удобно работать парсеру.

В моём конфиг‑языке токены такие:

- `T_WORD` — “слово” (server, listen, /var/www, 0.0.0.0:8080, index.html, ...)
- `T_LBRACE` — символ `{`
- `T_RBRACE` — символ `}`
- `T_SEMI` — символ `;`
- `T_EOF` — конец файла

Каждый токен содержит:
- `type` — тип токена (из списка выше)
- `text` — текст (например `"server"` или `"/var/www"`; для `{ } ;` тоже хранится текст)
- `line`, `col` — позиция токена для сообщений об ошибках парсинга

---

##### 4.1 Внутреннее состояние Tokenizer: `current_`, `line_`, `col_`

Tokenizer читает файл **посимвольно** и держит “текущий символ” в поле:

- `current_` — текущий символ (в `int`, чтобы различать обычные байты и специальный маркер `EOF`)
- `line_` — текущая строка (начинается с 1)
- `col_` — текущая колонка (в этой реализации увеличивается при чтении символов)

В конструкторе Tokenizer:
1) открывается файл
2) вызывается `advance()` — и в `current_` попадает **первый символ файла**

Функция `advance()`:
- читает следующий символ из `file_.get()`
- обновляет `(line_, col_)`:
  - если символ `'\n'` → `line_++`, `col_=0`
  - иначе → `col_++`

Это нужно, чтобы парсер мог сказать:  
`config parse error at line 12, col 7: expected ';'`

---

##### 4.2 `Tokenizer::next()` — “диспетчер токенов”

Когда парсер хочет следующий токен, он вызывает `Tokenizer::next()`. Она работает так:

1) вызывает `skipSpacesAndComments()` — пропускает мусор:
   - пробелы `' '`, `'\t'`
   - переводы строк `'\n'`
   - `'\r'` (важно, если файл с Windows‑переводами строк)
   - комментарии `# ...` до конца строки

2) если `current_ == EOF` → вернуть `T_EOF`

3) иначе смотрит на `current_`:
   - если это `{` → вернуть `T_LBRACE` и сделать `advance()`
   - если это `}` → вернуть `T_RBRACE` и сделать `advance()`
   - если это `;` → вернуть `T_SEMI` и сделать `advance()`
   - иначе → вернуть `readWord()` (то есть `T_WORD`)

Ключевая идея: `next()` решает, какой токен начинается в `current_`.

---

##### 4.3 Пропуск пробелов и комментариев: `skipSpacesAndComments()`

Эта функция крутится в цикле, пока видит “неважные” символы:

- если пробел/таб/CR/LF → `advance()` и продолжаем
- если `#` → это комментарий:
  - делаем `advance()` до конца строки (пока не `'\n'` или `EOF`)
  - потом снова продолжаем пропуск пробелов/переводов строк

Важно: комментарий начинается с `#` **вне слова**.  
То есть строка:

```nginx
root /var/www; # comment
```

после `;` увидит `#` и выкинет всё до конца строки.

---

##### 4.4 Что такое `T_WORD` в моём языке: `readWord()`

“Слово” (`T_WORD`) — это последовательность символов, которая заканчивается при встрече:

- пробела/таба/перевода строки (`' ' '\t' '\r' '\n'`)
- одного из специальных символов: `{`, `}`, `;`
- символа `#` (потому что дальше начинается комментарий, но только после завершения слова)

То есть `readWord()` читает:

- `server` → один токен `T_WORD("server")`
- `0.0.0.0:8080` → один токен `T_WORD("0.0.0.0:8080")` (внутри двоеточие разрешено)
- `/var/www` → один токен `T_WORD("/var/www")`
- `index.html` → один токен `T_WORD("index.html")`

А вот что важно как ограничение:
- кавычек нет → строка `root "/var/www site";` не будет работать (она разобьётся на токены странно)
- escape‑последовательностей нет

---

##### 4.5 Пример: как именно токены получаются из `simple.conf`

Исходный текст:

```nginx
server {
        listen 0.0.0.0:8080;
        root /var/www;
        index index.html;
}
```

Tokenizer будет выдавать токены в таком порядке (упрощённо, без line/col):

1) `T_WORD("server")`  
2) `T_LBRACE("{")`  
3) `T_WORD("listen")`  
4) `T_WORD("0.0.0.0:8080")`  
5) `T_SEMI(";")`  
6) `T_WORD("root")`  
7) `T_WORD("/var/www")`  
8) `T_SEMI(";")`  
9) `T_WORD("index")`  
10) `T_WORD("index.html")`  
11) `T_SEMI(";")`  
12) `T_RBRACE("}")`  
13) `T_EOF`

пробелы и переводы строк не становятся токенами — они просто разделители.

---

##### 4.6 Зачем вообще нужен отдельный Tokenizer (а не парсить строками)

Разделение Tokenizer/Parser даёт:
- парсер работает с понятными атомами (`WORD`, `{`, `}`, `;`), а не с символами
- проще и точнее ошибки (line/col)
- парсер не засоряется логикой “пропуска пробелов/комментариев”
- проще расширять язык конфига (например добавить кавычки или новые токены)

Tokenizer отвечает на вопрос: **“что написано?”** (какие токены идут)  
Parser отвечает на вопрос: **“это правильно по грамматике?”** и **“что это значит?”** (директивы, структуры)
---

#### Шаг 5 — `ConfigParser::parseConfig()` (верхний уровень)
После конструктора `ConfigParser`:
- `nextToken_ == T_WORD("server")` (первый lookahead)

`parseConfig()` делает:
1) создаёт пустой `Config cfg`
2) пока `nextToken_ != T_EOF`:
   - ожидает слово `server` на верхнем уровне
   - съедает его (`consumeToken()`)
   - парсит `server` блок (`parseServer()`) и добавляет в `cfg.servers`

После успешного `consumeToken()` на слове `server`:
- `nextToken_` становится `{`

---

#### Шаг 6 — `ConfigParser::parseServer()` (server-блок)
`parseServer()`:
1) ожидает `{` через `expect(T_LBRACE, ...)`
   - `expect()` проверяет текущий `nextToken_`, затем двигает поток (`consumeToken()`)
2) затем крутит цикл “пока не `}`”:
   - если текущий токен — `location`, парсит location блок
   - иначе считает это обычной директивой и вызывает `parseServerDirective()`

В нашем конфиге location нет, поэтому парсим только директивы.

---

#### Шаг 7 — директива `listen 0.0.0.0:8080;`
`parseServerDirective()` делает 3 шага:

1) читает имя директивы в `nameTok` (это `T_WORD("listen")`) и делает `consumeToken()`
2) читает аргументы до `;` функцией `readArgsUntilSemi()`:
   - собирает `args = ["0.0.0.0:8080"]`
   - съедает `;`
3) применяет директиву: `applyServerDirective(nameTok, args, srv)`
   - разбивает `host:port`
   - строго парсит порт (1..65535)
   - добавляет в `srv.listens`

После директивы `listen` следующий lookahead:
- `nextToken_ == T_WORD("root")`

---

#### Шаг 8 — директива `root /var/www;`
Аналогично:
- `nameTok = "root"`
- `args = ["/var/www"]`
- применяем:
  - `srv.hasRoot = true`
  - `srv.root = "/var/www"`

---

#### Шаг 9 — директива `index index.html;`
Аналогично:
- `nameTok = "index"`
- `args = ["index.html"]`
- применяем:
  - `srv.hasIndex = true`
  - `srv.index = "index.html"`

---

#### Шаг 10 — закрытие server-блока `}`
Когда `nextToken_ == T_RBRACE("}")`:
- `parseServer()` вызывает `expect(T_RBRACE, ...)` и съедает `}`
- возвращает заполненный `ServerConfig`

После этого:
- `nextToken_ == T_EOF`

---

#### Шаг 11 — завершение `parseConfig()`
`parseConfig()` видит `T_EOF`, завершает цикл и возвращает `Config cfg`.

Итоговая структура:

- `cfg.servers.size() == 1`
- `cfg.servers[0].listens.size() == 1`
  - host `"0.0.0.0"`, port `8080`
- `cfg.servers[0].hasRoot == true`, root `"/var/www"`
- `cfg.servers[0].hasIndex == true`, index `"index.html"`

---

### 2.4 Что происходит после загрузки конфига (кратко)
После того как `Config cfg` готов:
1) создаётся `Server(cfg)` → поднимаются listening sockets по `listen`
2) запускается `Server::run()` → один `poll()` обслуживает:
   - `accept()` новых клиентов на listen fd
   - `recv()` запросов на client fd
   - `send()` ответов на client fd

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

## Архитектура проекта (модули и ответственность)

Цель: быстро понять, **кто за что отвечает**, и как данные/события проходят через систему.

---

### Server (Server.hpp / Server.cpp)
**Роль:** оркестратор неблокирующего event loop и владелец всех fd.  
**Знает про:** `poll()`, listen sockets, accept, таблицу соединений.  
**НЕ знает про:** HTTP парсинг, построение ответов (это в Connection/Http*).

**Ключевые поля:**
- `Config cfg_` — загруженная конфигурация (набор server blocks).
- `std::vector<int> listenFds_` — все listening sockets.
- `std::map<int, std::size_t> listenFdToServerIndex_` — связь “listen fd → какой server block (индекс в cfg_.servers)”.
- `std::map<int, Connection> connections_` — активные соединения `clientFd -> Connection`.
- `std::vector<pollfd> pollFds_` — текущий набор fd для `poll()`.

**Ключевые методы:**
- `setupListenSockets()`  
  Создаёт слушающие сокеты по конфигу (`socket/bind/listen`), ставит `O_NONBLOCK`, заполняет `listenFds_` и `listenFdToServerIndex_`.

- `buildPollFds()`  
  Пересобирает `pollFds_` каждый тик:
  - listen fd: `events = POLLIN`
  - client fd: `events = Connection::wantedPollEvents()`

- `acceptPendingConnections(listenFd)`  
  Делает `accept()` в цикле (пока accept не перестанет возвращать fd), переводит clientFd в `O_NONBLOCK`, создаёт `Connection(clientFd, &cfg_, serverIndex)`.

- `run()`  
  Главный цикл:
  1) `buildPollFds()`
  2) `poll()`
  3) `accept` на listen fd
  4) `onReadable/onWritable` на client fd
  5) закрытие соединений

---

### Connection (Connection.hpp / Connection.cpp)
**Роль:** “контекст одного клиентского соединения”. Управляет состоянием чтения/записи и HTTP жизненным циклом запроса.

**Ключевые поля:**
- `int fd_` — client socket.
- `State state_` — `READING` / `WRITING` / `CLOSING`.
- `HttpRequest request_` — парсер HTTP запроса (state machine).
- `std::string in_` — входной буфер (накапливаем recv кусками).
- `std::string out_` — выходной буфер (отправляем send частями).
- `const Config* cfg_` — доступ к конфигу.
- `std::size_t serverIndex_` — индекс server block, которому принадлежит соединение (через listenFd→serverIndex).

**Ключевые методы:**
- `wantedPollEvents()`  
  Возвращает, что мониторить:
  - READING → `POLLIN`
  - WRITING и `out_` не пуст → `POLLOUT`

- `onReadable()`  
  1) `recv()` → дописать в `in_`  
  2) определить лимиты:
     - `maxHeaderBytes` фиксированный
     - `maxBodyBytes` берётся из `cfg_->servers[serverIndex_]` если задан `client_max_body_size`
  3) `request_.parse(in_, maxHeaderBytes, maxBodyBytes)`
     - если ERROR → собрать `buildErrorResponse(status)` и перейти в WRITING
     - если COMPLETE → обработать метод/uri (на текущем этапе поддержан минимальный GET)
       - `GET /` → отдать `root/index` как `text/html`

- `onWritable()`  
  `send()` из `out_` → удалить отправленное → когда `out_` пуст, соединение закрывается (пока `Connection: close`).

---

### HttpRequest (HttpRequest.hpp / HttpRequest.cpp)
**Роль:** инкрементальный парсер HTTP запроса (state machine).  
**Состояния:** `HEADERS → BODY → COMPLETE` или `ERROR`.

**Ключевые идеи:**
- заголовки заканчиваются на `\r\n\r\n`
- ограничение на размер заголовков (431)
- проверка `Content-Length` против `maxBodyBytes` (413)
- body читается инкрементально: если bytes ещё не хватает → остаёмся в BODY

---

### HttpResponse (HttpResponse.hpp / HttpResponse.cpp)
**Роль:** сборка HTTP-ответа в строку (status line + headers + CRLF + body).  
**Используется Connection для формирования `out_`.**

**Поддержано на текущем этапе:**
- `buildErrorResponse(status)` (text/plain)
- `buildResponse(status, contentType, body)` (универсальный)

---

### ConfigParser/Tokenizer (ConfigParser.cpp / Tokenizer.cpp)
**Роль:** загрузка и разбор конфигурации.
- Tokenizer: превращает поток символов в токены (`WORD`, `{`, `}`, `;`, `EOF`).
- Parser: строит `Config` / `ServerConfig` / `LocationConfig`.

**Поддержанные директивы server-level (на текущем этапе):**
- `listen host:port;`
- `root path;`
- `index filename;`
- `client_max_body_size N;`
- `error_page code path;`

---

## Текущий функционал (зафиксировано)
- Сервер неблокирующий, один `poll()` на все listen+client sockets.
- Multi-port: разные `server` блоки обслуживаются разными listening sockets (listenFd→serverIndex).
- GET MVP:
  - `GET /` отдаёт файл `root/index` как `text/html`.
- Body limit:
  - если `Content-Length > client_max_body_size` → `413 Payload Too Large` на этапе парсинга запроса.
- Другие методы пока возвращают `405 Method Not Allowed`.

---

## Как быстро протестировать
```bash
# 1) Статика
curl -v http://127.0.0.1:8080/

# 2) Лимит body
# client_max_body_size 10;
curl -v http://127.0.0.1:8080/ -d '01234567890123456789'

# 3) Multi-port (два server блока с разными root)
curl -v http://127.0.0.1:8080/
curl -v http://127.0.0.1:8081/
```

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

## Flow: один запрос `GET /` от клиента (poll → accept → recv → parse → read file → send → close)

Ниже — симуляция “как реально бежит программа” при запросе `GET /` в текущей реализации (MVP: `Connection: close`, один запрос на соединение).

### Условия
- Сервер запущен с конфигом:
  ```nginx
  server {
    listen 127.0.0.1:8080;
    root ./www;
    index index.html;
    client_max_body_size 10;
  }
  ```
- В `./www/index.html` лежит страница.
- Клиент делает:
  ```bash
  curl -v http://127.0.0.1:8080/
  ```

---

### 0) Старт сервера: подготовка listening socket
1) `main()` загружает конфиг через `ConfigParser`.
2) `Server(cfg)` вызывает `setupListenSockets()`:
   - для каждого `listen host:port` создаётся `listenFd`:
     - `socket(AF_INET, SOCK_STREAM, 0)`
     - `setsockopt(SO_REUSEADDR)`
     - `fcntl(O_NONBLOCK)`
     - `bind(host, port)`
     - `listen(backlog)`
   - `listenFd` добавляется в `listenFds_`
   - сохраняется соответствие: `listenFdToServerIndex_[listenFd] = serverIndex`

На этом этапе сервер ещё никому не отвечает — он просто “слушает”.

---

### 1) Event loop tick: сбор fd и ожидание событий (`poll`)
В `Server::run()` начинается бесконечный цикл:

1) `buildPollFds()` строит `pollFds_`:
   - для каждого `listenFd`: `events = POLLIN` (ждём новые подключения)
   - для каждого `clientFd` из `connections_`: `events = wantedPollEvents()`

2) `poll(pollFds, timeout=1000ms)`:
   - блокируется (не busy-loop) до появления событий или таймаута.

---

### 2) Клиент подключается → `POLLIN` на listenFd → `accept()`
Когда клиент делает TCP connect, у listen socket появляется событие `POLLIN`:

1) `run()` видит `revents & POLLIN` для listenFd
2) вызывает `acceptPendingConnections(listenFd)`
3) внутри `acceptPendingConnections()` в цикле:
   - `clientFd = accept(listenFd, ...)`
   - `fcntl(clientFd, O_NONBLOCK)`
   - вычисляет `serverIndex` по `listenFdToServerIndex_[listenFd]`
   - создаёт `Connection(clientFd, &cfg_, serverIndex)`
   - кладёт в `connections_[clientFd]`

Теперь соединение существует, но запрос ещё не прочитан.

---

### 3) Следующий tick: `POLLIN` на clientFd → `recv()` → накопление в `in_`
После connect клиент присылает HTTP запрос (байты). На `clientFd` возникает `POLLIN`:

1) `buildPollFds()` включает `clientFd` с `events = POLLIN` (потому что `Connection.state_ == READING`)
2) `poll()` возвращает, `revents` содержит `POLLIN`
3) `Server::run()` вызывает `Connection::onReadable()`

Внутри `Connection::onReadable()`:
1) `recv(fd_, buf, 4096)` читает “сколько дали сейчас”
2) эти байты добавляются в `in_`:
   - `in_.append(buf, n)`

Ключ: TCP — поток, поэтому один `recv()` может принести:
- только часть заголовков,
- заголовки + кусок body,
- заголовки + всё body,
- несколько запросов подряд (в будущем).

---

### 4) Парсинг запроса: `HttpRequest::parse(in_, maxHeaderBytes, maxBodyBytes)`
После пополнения `in_` Connection вызывает HTTP парсер:

1) определяет лимиты:
   - `maxHeaderBytes` фиксированный (например 16 KB)
   - `maxBodyBytes` берётся из `ServerConfig` своего `serverIndex_`:
     - если `hasClientMaxBodySize` → использовать `clientMaxBodySize`
     - иначе дефолт (например 1 MB)

2) вызывает:
   ```cpp
   st = request_.parse(in_, maxHeaderBytes, maxBodyBytes);
   ```

`HttpRequest::parse()` работает как state machine:

#### 4.1 HEADERS
- ищет `\r\n\r\n`
- если не нашёл — возвращает `HEADERS` (нужно дочитать)
- если `in_` разросся > `maxHeaderBytes` и terminator не найден → `ERROR 431`

#### 4.2 Когда `\r\n\r\n` найдено:
- отделяет headersBlock от `in_`
- парсит request line + headers
- если есть `Content-Length` и он > `maxBodyBytes` → `ERROR 413`
- если body не нужен → `COMPLETE`
- иначе → `BODY`

#### 4.3 BODY
- ждёт, пока `in_.size() >= contentLength_`
- когда хватает — вырезает body, ставит `COMPLETE`

---

### 5) COMPLETE: простейший роутинг и чтение файла
Если `st == COMPLETE`, Connection начинает “обработку запроса”:

1) берёт `ServerConfig` по `serverIndex_`
2) проверяет метод:
   - если не `GET` → `405`
3) проверяет URI:
   - сейчас поддержан только `/`
   - если не `/` → `404`
4) строит путь к файлу:
   - `path = joinPath(srv.root, srv.index)` → например `./www/index.html`
5) читает файл:
   - `readFileToString(path, body)`
   - если не прочитали → `404` (или 500, позже уточним по errno)

---

### 6) Сборка ответа: `HttpResponse::buildResponse(...)`
Если файл прочитан:
- собирается ответ:
  - `HTTP/1.1 200 OK`
  - `Content-Type: text/html`
  - `Content-Length: <body.size()>`
  - `Connection: close`
  - пустая строка `\r\n`
  - тело (HTML)

Строка ответа кладётся в `out_`, а состояние переключается:
- `state_ = WRITING`

---

### 7) WRITING: `poll(POLLOUT)` → `send()` частями → закрытие
1) На следующем `buildPollFds()`:
   - `Connection::wantedPollEvents()` вернёт `POLLOUT`, потому что `state_ == WRITING` и `out_` не пустой.
2) `poll()` разбудит, когда сокет готов принимать данные (POLLOUT).
3) `Server::run()` вызовет `Connection::onWritable()`:
   - `send(fd_, out_.c_str(), out_.size(), 0)`
   - `out_.erase(0, sentBytes)`
   - если `out_` стал пуст → Connection говорит “готов закрываться”

Так как мы всегда отправляем `Connection: close`, после полной отправки ответа соединение закрывается:
- `Server::closeConnection(fd)` → `close(fd)` и удаление из `connections_`

---

## Итоговая картина (сжатая)
1) `poll` на listenFd → `accept` → появился `clientFd`
2) `poll` на clientFd(POLLIN) → `recv` → `HttpRequest::parse`
3) `GET /` → `readFile(root/index)` → `HttpResponse::buildResponse`
4) `poll` на clientFd(POLLOUT) → `send` → close

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

### 5. Текущее состояние и roadmap

- [x] multi-listen sockets
- [x] LL(1) config parser (nginx-like)
- [x] listenFd -> serverIndex mapping (per listening socket)
- [x] server-level client_max_body_size -> HttpRequest::parse(maxBodyBytes) (413 by Content-Length)

- [ ] GET static: serve `root + uri` (not only `/`), with basic path traversal protection
- [ ] Content-Type (MIME) by file extension
- [ ] location selection by URI (longest prefix match)
- [ ] effective config inheritance (server -> location): root/index/autoindex/client_max_body_size/allowed_methods/return/upload_dir
- [ ] autoindex (directory listing)
- [ ] upload_dir (POST)  *(after location selection & method policy)*
- [ ] keep-alive (Connection: keep-alive + multiple requests per TCP connection)
- [ ] CGI (later)
- [ ] chunked/multipart (not MVP)
