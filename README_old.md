# webserv (42)

## Общая информация о веб-серверах
### Что такое веб-сервер
Веб-сервер — это программа, принимающая HTTP-запросы по сети и возвращающая HTTP-ответы. Он связывает клиент (браузер/скрипт) и ресурсы (файлы, приложения, CGI).

### Зачем он нужен
- отдача статических файлов (HTML/CSS/JS, изображения)
- маршрутизация запросов по URL
- выполнение динамики через CGI/приложения
- контроль доступа, лимиты, логирование

### Где применяется
- сайты/веб-приложения
- API-сервисы
- reverse proxy (в нашем проекте частично)

## Требования проекта (кратко)
- C++98, `-Wall -Wextra -Werror`, без Boost и внешних библиотек
- устойчивость: не падать и не зависать
- неблокирующий сервер, один `poll()` (или equivalent) на весь I/O
- поддержка GET/POST/DELETE
- загрузка файлов (upload)
- конфиг в стиле NGINX (без regex)
- CGI по расширению файла, минимум один тип (например, php-cgi или python)

## Конфигурационный файл: токенизация, парсинг, структуры

### Цель
Конфигурация описывает набор серверов (порт/хост), общие настройки и правила для маршрутов (location).
Формат вдохновлён nginx, но упрощён: без кавычек, без regex, минимальный набор директив.

### Структуры данных
- `Config` — корневой объект, содержит список `ServerConfig`.
- `ServerConfig` — настройки сервера (listen, root, index, autoindex, max body, error_page) + список `LocationConfig`.
- `LocationConfig` — правила для URL-префикса: разрешённые методы, root/alias/index/autoindex, upload, redirect, CGI по расширению.

#### Идея наследования настроек
Поля конфигурации имеют пары `hasX + X`.
Это позволяет отличать:
- “значение не задано в location → наследуется от server”
- от “значение задано явно → перекрывает server”

### Токенизация (Tokenizer)
Tokenizer читает файл посимвольно и выдаёт токены:
- `T_WORD` — слово (директива или аргумент)
- `{`, `}`, `;`
- `EOF`

Поддерживаются комментарии `# ...` до конца строки.
Кавычки и escape-последовательности не поддерживаются сознательно (упрощение языка).

### Парсер (ConfigParser)
Парсер реализует простую грамматику:
- верхний уровень: только блоки `server { ... }`
- внутри server: директивы `name args...;` и блоки `location <prefix> { ... }`
- внутри location: только директивы

На синтаксических/семантических ошибках выбрасывается `std::runtime_error` с координатами `line:col`.

### Loader (ConfigLoader)
`ConfigLoader::loadFromFile()` создаёт `ConfigParser` и возвращает результат парсинга.
`loadDefault()` формирует минимальную конфигурацию (один server, один listen по умолчанию).

## Конфигурационные примеры (из `conf/`)

### `minimal.conf` (самый простой старт)
```nginx
server {
  listen 127.0.0.1:8080;
}
```

**Поведение:**
- сервер слушает на `127.0.0.1:8080`
- остальные параметры берутся из значений по умолчанию (в коде: `ListenConfig` default / отсутствие `root/index` → запросы к файловой системе дают 500/403 в зависимости от логики handler’а)

### `simple.conf` / `default.conf` (статический сайт)
```nginx
server {
    listen 0.0.0.0:8080;
    root ./www;
    index index.html;
}
```

**Поведение:**
- `GET /` → отдаёт `./www/index.html`
- `GET /dir/`:
  - если есть `index` → отдаст `index`
  - иначе при `autoindex on` → покажет листинг
  - иначе 403

### `tester.conf` (покрытие методов + body limit + alias + CGI)
```nginx
server {
    listen 127.0.0.1:8080;

    root ./www;
    index index.html;

    location / {
        allow_methods GET;
    }

    location /post_body {
        allow_methods POST;
        client_max_body_size 100;
    }

    location /directory/ {
        allow_methods GET POST;
        alias ./YoupiBanane/;
        index youpi.bad_extension;
        autoindex off;
        cgi .bla ./cgi_tester;
    }
}
```

**Что демонстрирует:**
- `allow_methods` → 405 если метод не разрешён
- `client_max_body_size` → 413 если Content-Length больше лимита
- `alias` → “перебазирование” URI внутрь другой директории
- `cgi <ext> <executable>` → запуск CGI по расширению `.bla` через `./cgi_tester`

## Файловая структура проекта

```
webserv/
├─ README.md
├─ Makefile
├─ include/
│  ├─ Autoindex.hpp
│  ├─ Cgi.hpp
│  ├─ CgiHandler.hpp
│  ├─ Colors.hpp
│  ├─ Config.hpp
│  ├─ ConfigLoader.hpp
│  ├─ ConfigParser.hpp
│  ├─ ConfigTokenizer.hpp
│  ├─ Connection.hpp
│  ├─ EffectiveConfig.hpp
│  ├─ Filesystem.hpp
│  ├─ FilesystemHandler.hpp
│  ├─ HttpReply.hpp
│  ├─ HttpRequest.hpp
│  ├─ HttpResponse.hpp
│  ├─ Log.hpp
│  ├─ Mime.hpp
│  ├─ Path.hpp
│  └─ Server.hpp
├─ src/
│  ├─ main.cpp
│  ├─ Server.cpp
│  ├─ Connection.cpp
│  ├─ HttpRequest.cpp
│  ├─ HttpResponse.cpp
│  ├─ HttpReply.cpp
│  ├─ Mime.cpp
│  ├─ Autoindex.cpp
│  ├─ Filesystem.cpp
│  ├─ FilesystemHandler.cpp
│  ├─ Path.cpp
│  ├─ Cgi.cpp
│  ├─ CgiHandler.cpp
│  ├─ Config.cpp
│  ├─ ConfigLoader.cpp
│  ├─ EffectiveConfig.cpp
│  ├─ ConfigTokenizer.cpp
│  └─ ConfigParser.cpp
├─ conf/
│  ├─ default.conf
│  ├─ minimal.conf
│  ├─ simple.conf
│  ├─ tester.conf
│  ├─ autoindex.conf
│  ├─ 2serv.conf
│  └─ my.conf
├─ www/
│  └─ (статический контент для тестов)
├─ YoupiBanane/
│  └─ (контент/структура под тестер из subject)
├─ tester
└─ cgi_tester
```

### Назначение директорий
- `include/` — заголовки модулей (интерфейсы).
- `src/` — реализации модулей.
- `conf/` — примеры конфигураций для демонстрации фич (multi-port, autoindex, upload, CGI).
- `www/` — статические файлы для ручных тестов браузером/curl.
- `YoupiBanane/` — набор файлов/страниц под проверяющий скрипт.
- `tester`, `cgi_tester` — тестеры из задания (прогоняем регулярно, фиксируем несовпадения).

## Архитектура (компоненты)
- 
- 
- 
- 
- 
- 
- 
## Модуль: HTTP Request (`HttpRequest`)

### Назначение
`HttpRequest` реализует *incremental parsing* входящего HTTP-запроса: превращает поток байтов (который приходит кусками через `recv`) в структурированный объект:
- request line: `METHOD URI VERSION`
- headers (case-insensitive)
- body (Content-Length или Transfer-Encoding: chunked)

### Вход / выход
- **Вход:** `std::string &buffer` — накопленный входной буфер соединения.  
  Парсер *потребляет* байты из `buffer` (через `erase`), когда они успешно распознаны.
- **Выход:** `State`:
  - `HEADERS` — заголовки ещё не собраны (`\r\n\r\n` не найден)
  - `BODY` — заголовки распознаны, ждём тело
  - `COMPLETE` — запрос готов
  - `ERROR` — запрос некорректен (через `getErrorStatus()`)

### Ограничения и защиты
- `maxHeaderBytes`: если конец заголовков не найден и буфер разросся — возвращаем ошибку `431`.
- `maxBodyBytes`: ограничение размера тела:
  - для Content-Length — проверяется после парсинга заголовков (`413`)
  - для chunked — проверяется во время “разчанкивания” (`413`)

### Поддержка Transfer-Encoding: chunked
Для chunked запросов сервер **обязан unchunk** тело перед передачей в CGI/обработчики.
`HttpRequest` делает это в `parseChunkedBody()` и сохраняет результат в `body_`.
EOF для CGI означает конец тела: chunked поток *не* передаётся в CGI как есть.

### Важные детали реализации
- Заголовки нормализуются в lower-case ключи (`toLower`), чтобы доступ был case-insensitive.
- Дубликаты заголовков: хранится последнее значение (MVP поведение).
- Если одновременно `Transfer-Encoding: chunked` и `Content-Length` — считается ошибкой (400).

### Notes / возможные улучшения (можно выкинуть перед сдачей)
- [Naming] `State::HEADERS` можно назвать `PARSING_HEADERS`, `BODY` -> `PARSING_BODY` (чтобы было очевидно, что это “процесс”, а не “часть запроса”).
- [Behavior] Сейчас парсер строго требует CRLF внутри header block. Это ок для браузеров, но стоит помнить: некоторые клиенты шлют `\n` только (в проекте можно игнорировать).
- [Performance] `std::string::erase(0, n)` и `substr` копируют данные. Для 42 это нормально, но при желании можно перейти на буфер + offset.

## Модуль: HTTP Response builder (`HttpResponse`)

### Назначение
`HttpResponse` — “фабрика строк” HTTP-ответов.  
Он преобразует решение сервера (код/тип/тело/редирект) в готовую последовательность байтов, которую `Connection` отправляет через `send()`.

### Текущие функции
- `buildErrorResponse(status)` — дефолтный текстовый body для ошибок, если нет кастомной error_page.
- `buildResponse(status, contentType, body)` — обычный ответ.
- `buildRedirectResponse(status, target)` — редирект с заголовком `Location`.

### Notes / возможные улучшения (можно выкинуть перед сдачей)
- [Naming] namespace `HttpResponse` логичнее назвать `HttpResponseBuilder` или `HttpSerializer` (меньше путаницы со структурой/моделью ответа).
- [Behavior] Сейчас всегда ставится `Connection: close`. Это упрощение. Если позже будет keep-alive — логика заголовков переедет сюда.
- [Coverage] `reasonPhrase()` покрывает базовые коды. При расширении нужно синхронизировать со статусами из остальных модулей.

## Модель ответа: `Http::HttpReply`

### Назначение
`Http::HttpReply` — внутренний “результат обработки запроса” до сериализации в HTTP строку.  
Он отделяет *решение* (что ответить) от *форматирования* (как выглядит HTTP).

### Варианты ответа
- `REPLY_NORMAL`: status + content-type + body
- `REPLY_REDIRECT`: redirectCode + Location
- `REPLY_ERROR`: status (и опционально body)

### Notes / возможные улучшения (можно выкинуть перед сдачей)
- [Design] Сейчас есть и `Http::HttpReply`, и `HttpResponse::*` — это две конкурирующие модели. В долгую лучше оставить что-то одно:
  - либо `HttpReply` как модель + отдельный serializer `HttpResponse::build(HttpReply)`
  - либо без `HttpReply`, сразу строить строку ответа
- [Naming] `HttpReply` можно назвать `Reply` или `HandlerResult` (если это именно результат роутинга/хендлера).
- [Semantics] Для `REPLY_ERROR` поле `body` можно трактовать как “кастомное error body”, иначе генерировать дефолт.

## Конфиг: `EffectiveConfig`

### Назначение
`EffectiveConfig` — “слитые” настройки (server + location), предназначенные для использования на этапе обработки запроса.
Идея: парсер (`ConfigParser`) только читает файл и отмечает `hasX`, а логика наследования/мерджа выполняется отдельным слоем.

### Поля
Содержит набор параметров, применимых к конкретному запросу/маршруту:
- root/alias/index/autoindex
- client_max_body_size
- allow_methods
- upload_dir
- return (redirect)

### Notes / возможные улучшения (можно выкинуть перед сдачей)
- [Naming] `EffectiveConfig` можно назвать `ResolvedConfig` / `MergedConfig` — более стандартные термины.
- [Duplication] Есть пересечение полей с `LocationConfig`. Нормально, если `EffectiveConfig` — итог после наследования.

## Модуль: Server (event loop на `poll`)

### Назначение
`Server` — оркестратор файловых дескрипторов и владелец единственного event loop.
Он отвечает за:
- создание listening sockets (по конфигу: несколько server blocks × несколько listen)
- один общий `poll()` для всех I/O (listen + clients; в будущем сюда же должны попасть CGI pipes)
- `accept()` новых соединений
- маршрутизацию событий `POLLIN/POLLOUT` в соответствующий `Connection`
- закрытие соединений и очистку таблиц

`Server` **не знает HTTP** и не принимает решений “что отвечать”: эта логика живёт в `Connection` и ниже.

### Ключевые структуры
- `listenFds_` — список fd для `listen()`
- `pollFds_` — “снимок” всех fd на текущей итерации (listen + clients)
- `connections_` — `map<clientFd, Connection>`
- `listenFdToServerIndex_` — соответствие: на каком listenFd приняли соединение → какой server-block применять

### Flow `Server::run()`
1. `buildPollFds()` — собираем список fd: сначала listening, затем каждый клиент согласно `Connection::wantedPollEvents()`
2. `poll(..., timeout=1000ms)`
3. Для listen fd: если `POLLIN` → `acceptPendingConnections()`
4. Для client fd:
   - ошибки `POLLERR|POLLHUP|POLLNVAL` → close
   - `POLLIN` → `Connection::onReadable()`
   - `POLLOUT` → `Connection::onWritable()`

### Notes / улучшения / риски (черновик)
- [Subject] CGI pipes (stdin/stdout) — тоже I/O, которые по требованиям должны обслуживаться **через тот же poll**. Сейчас CGI у нас синхронный и блокирующий (см. `CgiHandler.cpp`), это потенциальный "grade 0" / hang.
- [Robustness] В текущем коде `poll()` вызывается с `&pollFds_[0]`. Если вдруг `pollFds_` пуст (теоретически при баге/конфиге), это UB. На практике listenFds_ всегда не пуст, но можно защититься.
- [Design] `buildPollFds()` пересобирает vector каждый тик. Для MVP ок. Для оптимизации можно хранить pollfd стабильно и обновлять `events`/добавления/удаления инкрементально.
- [Timeouts] timeout=1000ms — заглушка. Для требования “request never hang indefinitely” позже нужен пер-соединение дедлайн (read timeout / cgi timeout) и timeout poll должен быть min(дедлайнов).
- [Cleanup] Нет деструктора `Server` для закрытия listen/client fd при аварийном выходе. Для 42 не всегда критично, но для “не падать никогда” полезно.
- [Rule about errno] В subject есть фраза про “строго запрещено чекать errno после read/write”. Здесь мы её используем для accept/recv/send (см. ниже в Connection). Это спорная интерпретация: корректный неблокирующий сервер обычно отличает `EAGAIN` от фатальных ошибок. Если проверяющий действительно настаивает “не смотреть errno вообще” — ок, но тогда мы обязаны проектировать так, чтобы любые “ложные” ошибки просто приводили к корректному закрытию соединения без краша.

## Модуль: Connection (state machine соединения)

### Назначение
`Connection` — логика протокола и состояние конкретного client socket:
- читает байты из сокета
- парсит HTTP инкрементально (через `HttpRequest`)
- выбирает конфиг (server + best location) и применяет политику (methods, redirects, body limit)
- выбирает обработчик: filesystem vs CGI
- формирует исходящий буфер `out_` (через `HttpResponse`)
- отправляет ответ порциями (partial send)
- управляет состоянием `READING/WRITING/CLOSING`

### Состояния
- `READING`: ждём `POLLIN`, читаем из сокета, парсим HTTP
- `WRITING`: ждём `POLLOUT`, отправляем `out_` через `send()`
- `CLOSING`: сигнал серверу закрыть fd (пока закрываем сразу после ответа, т.к. `Connection: close`)

### Flow `Connection::onReadable()`
1. `recv()` → append в `in_`
2. `HttpRequest::parse(in_, maxHeaderBytes, maxBodyBytes)`
3. Реакция:
   - `ERROR` → готовим error response и переходим в `WRITING`
   - `BODY` → продолжаем читать; ранняя проверка location-level body limit (по Content-Length)
   - `COMPLETE` → обработка:
     - выбираем `LocationConfig` по “самый длинный prefix”
     - строим `EffectiveConfig` (server defaults + location overrides)
     - redirects: trySlashRedirect, return (redirect)
     - allow_methods → 405
     - далее: CGI или filesystem handler → `prepareReply(...)`

### Flow `Connection::onWritable()`
1. `send()` из `out_` (возможно частично)
2. `erase(0, n)` удаляет отправленную часть
3. когда `out_` пуст → соединение закрывается (пока всегда `Connection: close`)

### Notes / улучшения / риски (черновик)
- [Critical] `recv()` и `send()` на non-blocking сокетах могут вернуть `-1` с `EAGAIN/EWOULDBLOCK` даже после poll (редко, но бывает; race condition). Сейчас любой `n < 0` → false → close. Это может приводить к случайным disconnect под нагрузкой.
  - Если subject “запрещает errno” строго, то это тяжело исправлять корректно. Но на практике многие решения всё-таки проверяют `EAGAIN` и не закрывают соединение.
- [Subject wording] Запрет “не смотреть errno после read/write” в subject обычно означает: не стройте логику на errno в стиле “если EAGAIN, то…” *без poll*. Но после poll проверка `EAGAIN` — нормальная часть неблокирующего I/O. (Оставить как note: уточнить по чеклисту/внутренним правилам оценщика.)
- [HTTP] Сейчас сервер всегда отвечает `Connection: close`. Это упрощает state machine, но значит keep-alive не поддерживаем (в subject это не требуется).
- [Architecture] В `onReadable()` смешаны уровни:
  1) network I/O
  2) HTTP parsing
  3) config merge
  4) routing (location selection)
  5) handlers (filesystem/cgi)
  6) response serialization
  Для MVP ок, но усложняет отладку CGI. Кандидат на выделение “RequestHandler” слоя.
- [Naming] `in_`/`out_` можно назвать `recvBuffer_`/`sendBuffer_` — удобнее при дебаге.
- [Limits] maxBodyBytes берётся из server-level на этапе парсинга, а location-level применяется позже (в BODY). Это правильный подход, но важно: для chunked запросов `Content-Length` после unchunk выставляется только в конце. Значит location limit для chunked будет проверен позже (внутри `HttpRequest::parseChunkedBody` он уже сравнивает с maxBodyBytes, который сейчас server-level). Если хотим location-level для chunked — надо передавать eff.clientMaxBodySize в парсер (или иметь два лимита).
- [CGI] Вызов `Http::buildCgiReply(...)` сейчас синхронный и может блокировать весь event loop. Это противоречит “server must remain non-blocking” и “one poll for all I/O”.
  Рекомендация: перевод CGI на async-job с pipes в poll, иначе возможны hangs/0 баллов.
- [Redirect helper] `tryRedirectToSlashLocation()` реализует nginx-подобную логику “добавить / если есть location для директории”. Хорошая фича для UX.
- [Memory] `out_.erase(0, n)` на больших ответах потенциально O(n) копии. Для MVP норм; если будут большие файлы — лучше отправлять через offset или использовать sendfile/streaming (subject не требует sendfile).

## Конфиг-мердж в runtime: `selectLocation()` + `buildEffectiveConfig()`

### Выбор location
`selectLocation()` выбирает `LocationConfig` по принципу **longest prefix match**:
- матчится, если URI начинается с `location.prefix`
- выбирается максимальная длина префикса (nginx-like поведение без regex)

### EffectiveConfig
`buildEffectiveConfig(server, location)` применяет наследование:
1) берём server-level defaults
2) поверх накладываем location overrides (если `hasX`)

В `EffectiveConfig` попадают параметры маршрута:
- root/alias/index/autoindex
- client_max_body_size
- allow_methods
- upload_dir
- return (redirect)

### Notes / улучшения / риски (черновик)
- [Duplication] Поля дублируют `LocationConfig`. Это нормально, если `EffectiveConfig` — итоговая “готовая политика”, но важно держать единый источник правды.
- [CGI config] CGI пока не попадает в `EffectiveConfig` (он в `LocationConfig`). Это ок, но тогда “решение CGI” должно быть централизовано в одном месте (например `isCgiRequest(loc, uri)`).
- [Naming] `selectLocation()` можно назвать `matchLocationLongestPrefix()` — длинно, но абсолютно очевидно.

## Модуль: Path / безопасность путей (`Path::safeJoin`, `safeJoinAlias`)

### Назначение
Модуль `Path` отвечает за преобразование URI → filesystem path, с базовой безопасностью:
- удаление query (`?x=1`)
- URL decode `%XX` (с валидацией)
- нормализация сегментов (`.` и `..`)
- запрет выхода за пределы root (path traversal)
- запрет encoded slash (`%2F`) как политика безопасности (упрощает защиту)

### `safeJoin(root, rawUri, outFsPath, outStatus)`
Алгоритм:
1) запрещает `#` во входном URI (строгая политика)
2) отрезает query: `uriPathOnly`
3) декодирует `%XX` (ошибка → 400)
4) требует, чтобы путь начинался с `/` (иначе 400)
5) разбивает на сегменты и нормализует:
   - игнорирует `""` и `"."`
   - `".."` делает `pop_back()`, но если выйти некуда → 403 (traversal)
6) склеивает `root + segments` через `Fs::joinPath`

Возвращаемые статусы через `outStatus`:
- `400` — некорректный URI / неправильное кодирование
- `403` — попытка выйти за root (`..`)
- `200` — успех

### `safeJoinAlias(aliasBase, locPrefix, rawUri, ...)`
Alias реализуется как “rebasing”:
- проверяем, что URI матчится на `locPrefix`
- отрезаем prefix → получаем tail
- превращаем tail в “как будто новый URI внутри aliasBase” (добавляем `/`)
- вызываем `safeJoin(aliasBase, rebasedUri, ...)`

### Notes / возможные улучшения (черновик)
- [Naming] `safeJoin` можно назвать `mapUriToFsPathSafe` (длинно, но прозрачно).
- [Policy] Запрет `%2F` — осознанная политика. Она упрощает безопасность, но отличается от nginx/Apache.
- [Edge] `safeJoinAlias()` использует `startsWithPrefix(rawUri, locPrefix)` на `rawUri` (который может содержать `?query`), но итоговая нормализация всё равно режет query внутри `safeJoin()`. Работает, но стоит помнить.

## Модуль: Static filesystem handler (`FilesystemHandler`)

### Назначение
`buildFileSystemReply(eff, loc, uri)` реализует обработку “обычных” запросов к файловой системе:
- маппинг URI → fs path через `safeJoin/safeJoinAlias`
- проверка через `Fs::classifyPath`
- директория:
  - редирект `/dir` → `/dir/`
  - index файл (`eff.index`) если задан
  - autoindex если разрешён
  - иначе 403
- файл: читается целиком в память (`Fs::readFileToString`) и отдаётся как `HttpReply`

### Notes / ограничения (черновик)
- [TODO] `POST upload` и `DELETE` пока не реализованы.
- [Perf] файлы читаются целиком в память — ок для MVP и маленьких файлов, но не для больших.
- [Naming] `FilesystemHandler` можно назвать `StaticFileHandler` (если он не будет заниматься upload/delete).


## CGI (текущее состояние реализации)

### Как сейчас работает
Сейчас CGI реализован синхронным вызовом из `Connection::onReadable()`:
- определяется CGI по расширению (`isCgiRequest`)
- строится env и filesystem path
- запускается `fork + execve`
- parent блокирующе пишет request body в stdin pipe и читает stdout целиком

### Важные замечания subject (то, что надо будет довести)
- CGI должен работать в корректной директории (`chdir(workDir)`) — сейчас делается.
- chunked requests: сервер обязан unchunk перед CGI — сейчас `HttpRequest` unchunk-ит.
- Если CGI не вернул `Content-Length`, EOF от stdout pipe является концом ответа — сейчас так и делается (readAll до EOF).

### Notes / улучшения / риски (черновик, критично)
- [Critical / grade risk] CGI сейчас блокирует event loop: `writeAll/readAll/waitpid` — всё синхронно и без poll. Это противоречит требованию “server must remain non-blocking” и “one poll for all I/O”.
- [Timeout] Нет таймаута на CGI → request может висеть бесконечно.
- [FD hygiene] В child/parent fd закрываются корректно, это хорошо (иначе EOF не придёт).
- [Env] env собирается под 42 tester (SCRIPT_NAME/PATH_INFO). Это ок как “compat mode”, но нужно держать в голове: CGI стандарт ожидает более строгие правила для SCRIPT_NAME (часто это путь до файла скрипта, а не location prefix).

## CGI модуль
### Цель
Запуск внешнего обработчика (php-cgi/python/...) и прокачка request body в stdin CGI, получение ответа CGI из stdout, конверсия в HTTP response.



## Конфиг: `cgi` директива и текущая реализация CGI

### Конфиг
В `location` можно задать:
```nginx
cgi .bla ./cgi_tester;
```
Где:
- `.bla` — расширение файла в URI
- `./cgi_tester` — исполняемый CGI handler

### Как определяется CGI запрос
`isCgiRequest(loc, uri)`:
- location должен иметь `hasCgi`
- берём расширение через `getExtension(uri)`
- проверяем наличие обработчика в `loc->cgiHandlers[ext]`

### Как сейчас выполняется CGI (синхронно)
`buildCgiReply(eff, loc, req)`:
1) вычисляет путь скрипта `scriptFsPath` (через `safeJoin`/`safeJoinAlias`)
2) проверяет существование файла (`Fs::classifyPath`)
3) строит `env` (REQUEST_METHOD, QUERY_STRING, SCRIPT_FILENAME, PATH_INFO, ...)
4) запускает CGI через `fork + execve`
5) parent:
   - пишет тело запроса в stdin CGI (если есть)
   - читает stdout CGI целиком до EOF
   - waitpid
6) парсит stdout CGI:
   - если есть `\r\n\r\n`, то header/body
   - `Status:` задаёт код ответа
   - `Content-Type:` задаёт тип
   - иначе по умолчанию `200 text/plain`

### Notes / важные TODO (черновик, критично)
- [Critical] Сейчас CGI блокирует event loop (нет poll на pipes, нет таймаута). По subject CGI нужно переводить на async через `poll()` и non-blocking pipes.
- [Design] `buildCgiReply()` делает слишком много (resolve path + env + spawn + parse output). Это кандидат на разбиение на 3-4 подкомпонента.
- [Config] Сейчас `EffectiveConfig` не содержит CGI-настроек, используется `LocationConfig`. Это ок, если держать “решение CGI” в одном месте.

## CGI: правильная интеграция в архитектуру (subject-compliant)

### Где живёт CGI
CGI — часть state machine конкретного соединения (`Connection`), потому что:
- CGI запускается в ответ на конкретный HTTP request
- CGI использует request body и формирует response
- управление временем/таймаутом относится к lifecycle этого request

### Server: dispatch по типам fd (чистая модель)
Чтобы поддержать “один poll() для всех I/O”, сервер мониторит не только client sockets, но и CGI pipes.
Для этого используется параллельный массив:
- `pollFds_[i]` — fd и события для poll()
- `fdEntries_[i]` — метаданные: что это за fd (listen/client/cgi stdin/cgi stdout) и какому Connection он принадлежит

Это избавляет от “угадывания” по fd и делает event loop расширяемым (например, для файловых стримов или таймеров).

## Текущий статус фич (честный чеклист)

### Реализовано
- неблокирующий сервер на `poll()` (listen + clients)
- incremental HTTP parsing:
  - request line + headers
  - Content-Length body
  - Transfer-Encoding: chunked (server unchunk’ит тело)
- routing по `location` (longest prefix match)
- root/index/alias + защита от path traversal через `safeJoin/safeJoinAlias`
- autoindex (directory listing)
- методы: политика `allow_methods` (возвращаем 405)
- redirect:
  - `return <code> <target>`
  - redirect `/dir` → `/dir/` если реально есть директория
- CGI (минимально): запуск по расширению + сбор env + чтение stdout

### Пока НЕ реализовано (TODO)
- Upload (`POST` сохранение файлов) и конфиг `upload_dir`
- HTTP method `DELETE`
- custom error pages (`error_page <code> <path>`)
- keep-alive (сейчас всегда `Connection: close`)
- таймауты (request timeout / CGI timeout)

### Notes / важные риски для оценки (черновик)
- [Critical] CGI сейчас синхронный (блокирующий `write/read/waitpid`). По subject CGI должен быть интегрирован в общий event loop через `poll()` (pipes тоже I/O). Сука, работаем над этим прямо сейчас!
- [Robustness] `recv/send` при non-blocking могут вернуть `-1` даже после poll (EAGAIN). Сейчас это трактуется как “close connection”. Под нагрузкой может давать нестабильность.


## Flow программы на примере `conf/simple.conf` (детально)

### Конфиг
```nginx
server {
    listen 0.0.0.0:8080;
    root ./www;
    index index.html;
}
```

### 0) Старт процесса (`main.cpp`)
1. Пользователь запускает:
   ```bash
   ./webserv conf/simple.conf
   ```
2. `main()`:
   - вызывает `ConfigLoader::loadFromFile(argv[1])`
   - создаёт `Server s(cfg)`
   - вызывает `s.run()`

**Notes / улучшения (черновик)**
- [CLI] Есть режим `--check-config`, это удобно для быстрой проверки синтаксиса и ошибок `line/col`.

---

### 1) Загрузка и парсинг конфига (`ConfigLoader` → `ConfigParser` → `Tokenizer`)
#### 1.1 Tokenizer (лексер)
`Tokenizer` читает файл посимвольно и выдаёт токены:
- `T_WORD` — слова (`server`, `listen`, `root`, `index`, аргументы)
- `{`, `}`, `;`
- `EOF`
Также пропускает пробелы и комментарии `#...`.

#### 1.2 Parser (грамматика)
`ConfigParser::parseConfig()` ожидает на top-level только блоки `server { ... }`.

Для `simple.conf` парсер создаёт:
- `Config cfg;`
- `cfg.servers[0]` типа `ServerConfig`

Заполняются поля:
- `srv.listens` получает `ListenConfig{ host="0.0.0.0", port=8080 }`
- `srv.hasRoot=true`, `srv.root="./www"`
- `srv.hasIndex=true`, `srv.index="index.html"`
- `srv.locations` остаётся пустым (нет location блоков)

**Если конфиг сломан**
- парсер кидает `std::runtime_error` с координатами `line/col`
- `main()` ловит исключение и печатает `Fatal: ...`

**Notes / улучшения (черновик)**
- [listen] Сейчас поддерживается только формат `host:port`. Это ок для проекта, но это ограничение относительно nginx.

---

### 2) Инициализация сервера (`Server::Server()` → `setupListenSockets()`)
После парсинга `main()` вызывает:
```cpp
Server s(cfg);
```

В конструкторе `Server`:
- сохраняется копия `cfg_`
- вызывается `setupListenSockets()`

#### 2.1 Создание listen socket (`createListenSocket(host, port)`)
Для `0.0.0.0:8080` сервер делает:
1) `socket(AF_INET, SOCK_STREAM, 0)` → получаем `listenFd`
2) `setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, 1)`  
   Чтобы можно было быстро перезапускаться без “Address already in use”
3) `fcntl(listenFd, F_SETFL, O_NONBLOCK)`  
   Listening socket становится non-blocking
4) `inet_pton(AF_INET, "0.0.0.0", &addr.sin_addr)`  
   Конвертируем строковый IP в бинарный
5) `bind(listenFd, ...)`  
   Привязываем к адресу/порту
6) `listen(listenFd, backlog=128)`  
   Переводим в режим ожидания входящих соединений

Результат:
- `listenFds_ = [listenFd]`
- `listenFdToServerIndex_[listenFd] = 0` (первый server block)

**Notes / улучшения (черновик)**
- [Multi-port] Архитектура поддерживает несколько listenFds_ (несколько server blocks / несколько listen директив).
- [IPv6] Пока только IPv4 (`sockaddr_in`). Для subject обычно достаточно.

---

### 3) Главный цикл событий (`Server::run()`)
Сердце программы — бесконечный цикл:
```cpp
while (true) {
    buildPollFds();
    poll(..., timeout=1000);
    accept...
    read/write clients...
}
```

#### 3.1 Построение массива `pollfd` (`buildPollFds()`)
Сборка списка fd происходит каждый тик:
1) В `pollFds_` добавляются все listen fd с `events = POLLIN`
2) Затем добавляются client fd из `connections_`, но уже с `events = Connection::wantedPollEvents()`

На старте (когда клиентов нет):
- `pollFds_` содержит только listen fd:
  - `pollFds_[0].fd = listenFd`
  - `pollFds_[0].events = POLLIN`

#### 3.2 Ожидание событий (`poll()`)
Вызов:
```cpp
poll(&pollFds_[0], pollFds_.size(), 1000);
```
- poll блокируется до 1000мс или до появления событий.
- если `eventCount <= 0` (таймаут или ошибка) — loop продолжает работу.

**Notes / улучшения (черновик)**
- [Timeouts] Сейчас timeout фиксированный (1000мс). Для “request never hang indefinitely” позже нужны дедлайны на соединения (read timeout) и на CGI.
- [Perf] Пересборка `pollFds_` каждый тик — норм для MVP.

---

### 4) Подключение клиента (accept)
Когда клиент подключается (например, браузер открывает `http://localhost:8080/`):
- на listen fd появляется `POLLIN`
- `Server` вызывает `acceptPendingConnections(listenFd)`

#### 4.1 `acceptPendingConnections()` (в цикле)
`accept()` делается в цикле, чтобы за один poll принять несколько клиентов из backlog:
1) `accept(listenFd, ...)` → возвращает `clientFd`
2) `setNonBlocking(clientFd)` → клиентский сокет тоже non-blocking
3) Создаётся `Connection(clientFd, &cfg_, serverIndex=0)`
4) Добавляется в `connections_[clientFd]`

После этого на следующем тике `buildPollFds()` добавит этот `clientFd` в мониторинг.

**Notes / улучшения (черновик)**
- [Rule errno] Код не различает причины ошибок accept() (не смотрит errno). Формально так проще и соответствует “не проверять errno”, но может ухудшить диагностику.

---

### 5) Чтение запроса (POLLIN → `Connection::onReadable()`)
Когда клиент отправляет HTTP запрос, например:
```http
GET / HTTP/1.1
Host: localhost:8080

```

#### 5.1 Poll решает “можно читать”
- `poll` ставит для clientFd событие `POLLIN` в `revents`
- `Server` находит `Connection &c`
- вызывает `c.onReadable()`

#### 5.2 Network layer: `recv()` и накопление `in_`
`Connection::onReadable()`:
1) `recv(fd_, buf, 4096, 0)`
2) если `n == 0` → клиент закрыл соединение → возвращаем false (Server закроет fd)
3) если `n < 0` → считаем ошибкой → возвращаем false (Server закроет fd)
4) если `n > 0` → `in_.append(buf, n)`

`in_` — накопительный буфер входных байтов.

**Notes / улучшения (черновик)**
- [Robustness] На non-blocking `recv` возможно `-1` (EAGAIN/EWOULDBLOCK) даже после poll. Сейчас это приводит к закрытию соединения.
- [DoS] Ограничение `maxHeaderBytes` защищает от бесконечных заголовков.

---

### 6) Парсинг HTTP запроса (`HttpRequest::parse()`)
После `recv()` Connection запускает парсер:
- `maxHeaderBytes = 16 * 1024`
- `maxBodyBytes` берётся из server-level `client_max_body_size`, но в simple.conf он не задан, значит остаётся дефолт.

Для `GET /` без body сценарий:
1) `HttpRequest::parse()` в состоянии `HEADERS` ищет `\r\n\r\n`
2) как только найдено:
   - вырезает headers из `in_`
   - парсит request line (метод/uri/version)
   - парсит header fields
   - определяет `hasContentLength_` и `hasChunked_`
3) так как нет Content-Length и не chunked:
   - `state_ = COMPLETE`

Connection получает `st == HttpRequest::COMPLETE`.

---

### 7) Выбор server/location и мердж конфигов (`selectLocation` + `buildEffectiveConfig`)
Для `simple.conf`:
- server block один: `serverIndex_=0`
- `srv.locations` пустой → `selectLocation` возвращает `NULL`

Дальше строится `EffectiveConfig eff = buildEffectiveConfig(srv, NULL)`:
- `eff.root = "./www"` (из server root)
- `eff.index = "index.html"` (из server index)
- `alias` отсутствует
- allow_methods отсутствует (значит разрешаем всё, пока не задано)
- autoindex отсутствует (значит поведение директорий: 403, если нет index и autoindex)

**Notes / улучшения (черновик)**
- [Policy] В этом проекте allow_methods задаётся только на location (server-level нет). Это осознанно, но можно расширить.

---

### 8) Redirects и метод-политики
В `Connection::onReadable()` идут проверки:

1) `tryRedirectToSlashLocation(...)`  
   Для `/` и отсутствия locations обычно ничего не делает.

2) `eff.hasRedirect` (директива `return`)  
   В simple.conf нет → пропускаем.

3) `isAllowedMethod(method, eff)`  
   allow_methods не задан → считаем “разрешено”.

4) `eff.hasRoot || eff.hasAlias`  
   root есть → ок.

---

### 9) Выбор handler’а: CGI vs filesystem
В simple.conf нет `cgi` директив, значит:
- `Http::isCgiRequest(loc, uri)` возвращает false (loc == NULL)

Вызывается:
```cpp
HttpReply rep = Http::buildFileSystemReply(eff, loc, uri);
```

---

### 10) Обработка статического контента (`FilesystemHandler::buildFileSystemReply`)
Для URI `/`:
- срабатывает special-case `if (uri == "/")`:
  1) если `eff.hasIndex == false` → 403  
     (но у нас index есть)
  2) `path = joinPath(eff.root, eff.index)` → `"./www/index.html"`
  3) `Fs::classifyPath(path)` → должен быть `PATH_FILE`
  4) `Fs::readFileToString(path, body)` → читает файл целиком в память
  5) `Http::guessContentType(path)` → `text/html` (если `.html`)
  6) возвращается `HttpReply` типа OK (200, content-type, body)

---

### 11) Сериализация ответа (`Connection::prepareReply` → `HttpResponse::*`)
`Connection::prepareReply(rep)`:
- превращает `HttpReply` в HTTP строку через `HttpResponse::buildResponse(...)`
- записывает результат в `out_`
- переводит состояние `state_ = WRITING`

Ответ выглядит примерно так:
```http
HTTP/1.1 200 OK
Content-Type: text/html
Content-Length: <N>
Connection: close

<body bytes>
```

---

### 12) Отправка ответа (POLLOUT → `Connection::onWritable()`)
На следующих тиках:
1) `Connection::wantedPollEvents()` возвращает `POLLOUT` (пока `out_` не пуст)
2) `poll` сообщает `POLLOUT`
3) `Server` вызывает `c.onWritable()`

`onWritable()`:
- делает `send(fd_, out_.c_str(), out_.size(), 0)`
- удаляет отправленное `out_.erase(0, n)`
- когда `out_` пуст:
  - возвращает `false`
  - `Server` закрывает соединение (`closeConnection(fd)`)

### Почему соединение закрывается всегда
Потому что ответы всегда содержат:
```
Connection: close
```
и логика Connection сделана под “один запрос → один ответ → закрыть”.

**Notes / улучшения (черновик)**
- [TODO] keep-alive: после отправки ответа нужно `request_.reset()`, очистить буферы и перейти обратно в `READING`, а не закрывать.
- [Robustness] non-blocking send может вернуть `-1` (EAGAIN) — сейчас это приведёт к close.

---

### Итоговый “сквозной” сценарий `GET /`
1) старт → парсинг simple.conf
2) listen на 0.0.0.0:8080
3) poll ждёт
4) accept клиента
5) recv запрос
6) HttpRequest парсит до COMPLETE
7) выбираем server, loc отсутствует
8) filesystem handler: `./www/index.html`
9) buildResponse → out_
10) send → close

---

### Notes / “что важно помнить” (черновик)
- Сервер **не блокируется на сети** благодаря `poll()` + O_NONBLOCK.
- Сейчас сервер **может блокироваться на CGI** (пока CGI синхронный).
- Статические файлы читаются блокирующим `read()` и целиком в память — это разрешено по subject (disk files без poll), но может быть тяжело для больших файлов.
