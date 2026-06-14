# 02 — Чек-лист защиты и тест-кейсы

Воспроизводимые проверки для ревью. Каждый кейс оформлен единообразно:
**что проверяем → команда → ожидаемое → почему так (ссылка на код)**.

Часть проверок уже автоматизирована в [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) — можно подсмотреть
готовые curl-ассерты. Бинари `./tester` и `./cgi_tester` из корня — официальные тестеры 42.

## Подготовка стенда

В начале задаём переменные, чтобы команды были копипастящимися и не зависели от хоста:

```bash
# --- общие переменные стенда ---
HOST=127.0.0.1
PORT=8080
BASE="http://$HOST:$PORT"
CONF=conf/tester.conf

# сборка с санитайзером
make re

# поднимаем сервер для всех тестов ниже
./webserv "$CONF" &
SRV_PID=$!
sleep 1   # дать сокетам подняться
```

В конце всех тестов: `kill "$SRV_PID"`.

---

## Сборка и базовая гигиена

| Проверка | Команда | Ожидаемое |
|---|---|---|
| Компиляция без варнингов | `make re` | бинарь `webserv`, **0 warnings** при `-Wall -Wextra -Werror -std=c++98` |
| Нет relink | `make && make` | второй вызов ничего не пересобирает |
| Проверка конфига | `./webserv --check-config conf/tester.conf` | `OK: conf/tester.conf`, код выхода 0 |
| Битый конфиг не роняет | `./webserv conf/НЕТ_ТАКОГО` | осмысленная ошибка `Fatal: ...`, код 1, без segfault |

> ASan включён в `Makefile:27`. Любой leak/heap-overflow во время тестов уронит процесс с отчётом —
> это и есть проверка требования «сервер не должен падать / течь».

---

## Функциональные тест-кейсы

### TC-01 — Базовый GET (mandatory)
```bash
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/"        # ожидаем: 200
```
> Почему: `Connection::onReadable` → `selectLocation` находит `location /` → `FilesystemHandler::buildFileSystemReply`
> отдаёт `www/index.html` (`src/FilesystemHandler.cpp:28`). Если `000` — сервер не поднялся; если `500` — не задан root.

### TC-02 — Несуществующий путь → 404
```bash
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/notfound"  # ожидаем: 404
```
> Почему: `Fs::classifyPath` вернёт `PATH_MISSING` → `pathKindToHttpStatus` = 404 (`src/Filesystem.cpp:99`).

### TC-03 — Редирект каталога без слэша → 301
```bash
curl -s -o /dev/null -D - "$BASE/directory" | grep -i '^location'   # ожидаем: Location: /directory/
```
> Почему: `Connection::tryRedirectToSlashLocation` (`src/Connection.cpp:288`) отдаёт `301` на `uri + "/"`,
> чтобы относительные ссылки внутри каталога не ломались. Затем `GET /directory/` должен дать `200`.

### TC-04 — Запрещённый метод → 405
```bash
curl -s -o /dev/null -w "%{http_code}\n" -X POST --data x "$BASE/"   # ожидаем: 405
```
> Почему: `location /` в `conf/tester.conf:8` разрешает только `GET`; `isAllowedMethod` (`src/Connection.cpp:180`)
> вернёт false → `buildErrorResponse(405)`.
>
> ⚠️ **Важный нюанс для защиты:** проверка метода намеренно **пропускает `DELETE`**
> (`if (method != "DELETE" && !isAllowedMethod(...))`, `src/Connection.cpp:622`) — это «хак для тестов».
> Поэтому `curl -X DELETE "$BASE/"` даст не 405, а 403 (от `handleDelete`, т.к. удалять каталог нельзя).
> Чтобы увидеть именно 405, используй метод, отличный от DELETE, — например `POST`. Это хороший вопрос
> на защите: «почему DELETE не уважает `allow_methods`?».

### TC-05 — `client_max_body_size` → 413 (переменная размера тела)
```bash
BODY_LEN=200                                              # лимит для /post_body = 100 (tester.conf:13)
PAYLOAD=$(head -c "$BODY_LEN" /dev/zero | tr '\0' 'A')
curl -s -o /dev/null -w "%{http_code}\n" -X POST --data "$PAYLOAD" "$BASE/post_body"   # ожидаем: 413
```
> Почему: проверка `eff.clientMaxBodySize` в `Connection::onReadable` (`src/Connection.cpp:576` и `:604`) → 413.
> Контроль: с `BODY_LEN=50` (меньше лимита) тот же запрос даёт `200` и тело `post_body ok`.

### TC-06 — CGI, мульти-интерпретатор (bonus)
```bash
curl -s "$BASE/cgi-bin/test.py"     # ожидаем: <h1>Hello from Python!</h1>
curl -s "$BASE/cgi-bin/test.sh"     # ожидаем: <h1>Hello from Bash!</h1>
```
> Почему: `Http::isCgiRequest` матчит расширение (`src/CgiHandler.cpp:264`) → `startCgi` форкается и зовёт
> `execve` интерпретатора из `cgi .py ...` / `cgi .sh ...` (`conf/tester.conf:27-28`) → `parseCgiOutput`
> разбирает заголовки CGI. Проверь, что в окружении есть интерпретатор из конфига (`/opt/pyenv/shims/python3`,
> `/bin/bash`); иначе скрипт упадёт и сервер вернёт `500` (а не зависнет — это тоже проверка).

### TC-07 — Cookies / session (bonus, cookie-jar)
```bash
JAR=$(mktemp)
curl -s -c "$JAR" "$BASE/session" >/dev/null   # 1-й визит: сервер ставит Set-Cookie session_id
curl -s -b "$JAR" "$BASE/session"              # 2-й визит: cookie читается, счётчик визитов растёт
```
> Почему: `HttpRequest::getCookieValue("session_id")` + `HttpResponse::buildResponseWithCookie`
> (`src/HttpResponse.cpp:136`) в ветке `/session` внутри `Connection::onReadable`.

### TC-08 — Upload файла (mandatory)
```bash
./webserv conf/upload.conf &  UP_PID=$!   ; sleep 1
curl -s -o /dev/null -w "%{http_code}\n" -X POST --data "hello upload" "$BASE/uploads"   # ожидаем: 2xx
ls -l www/uploads/                                                                       # файл появился
kill "$UP_PID"
```
> Почему: `location /uploads` с `upload_dir ./www/uploads` (`conf/upload.conf`) → `Connection::handleUpload`
> (`src/Connection.cpp:405`).

### TC-09 — DELETE файла (mandatory)
```bash
echo tmp > www/uploads/victim.txt
curl -s -o /dev/null -w "%{http_code}\n" -X DELETE "$BASE/uploads/victim.txt"   # ожидаем: 2xx
test -f www/uploads/victim.txt && echo "FAIL: файл всё ещё на месте" || echo "OK: удалён"
```
> Почему: `Connection::handleDelete` (`src/Connection.cpp:337`) маппит URI на путь через `safeJoin`
> и удаляет файл. Работает прямо на `conf/tester.conf` (DELETE обходит проверку `allow_methods`, см. TC-04).
> Удаление существующего файла даёт `200`; удаление каталога — `403`; несуществующего — `404`.

### TC-10 — Autoindex (mandatory)
```bash
./webserv conf/autoindex.conf &  AI_PID=$!  ; sleep 1
curl -s "$BASE/docs/" | grep -i '<a href'    # ожидаем: HTML-листинг каталога www/docs/
kill "$AI_PID"
```
> Почему: `location /docs/` с `autoindex on` → `Http::appendDirectoryListingHtml` (`src/Autoindex.cpp`).
> Контроль: `GET /` (где `autoindex off`) листинг **не** отдаёт.

### TC-11 — Path traversal заблокирован (безопасность)
```bash
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/../../etc/passwd"          # ожидаем: 4xx (не 200 с содержимым!)
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/%2e%2e/%2e%2e/etc/passwd"  # encoded .. тоже заблокирован
```
> Почему: `Http::safeJoin` нормализует `.`/`..` посегментно и бьёт `403`, если `..` выходит за пределы root
> (`src/Path.cpp:185-195`). encoded `%2e%2e` декодируется до `..` **до** проверки.

### TC-12 — Несколько серверов / портов (mandatory)
```bash
./webserv conf/2serv.conf &  MS_PID=$!  ; sleep 1
curl -s -o /dev/null -w "8080 -> %{http_code}\n" "http://$HOST:8080/"
curl -s -o /dev/null -w "8081 -> %{http_code}\n" "http://$HOST:8081/"
kill "$MS_PID"
```
> Почему: `setupListenSockets` открывает по сокету на каждый `listen` каждого `server{}` блока.
> Оба порта должны **отвечать** (код может быть 404/500, если каталоги `./www1`/`./www2` отсутствуют — важно,
> что сервер слушает оба порта и не падает).

### TC-13 — Устойчивость к «битому» запросу (сервер не виснет)
```bash
printf 'GET / HTTP/1.1\r\n' | nc -w1 "$HOST" "$PORT"   # неполный запрос, без завершающего \r\n\r\n
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/"      # сервер жив → снова 200
```
> Почему: `HttpRequest::parse` остаётся в состоянии `HEADERS` и ждёт данные, не блокируя event loop
> (`src/HttpRequest.cpp:140`). Закрытие/таймаут не роняют `Server::run`.

### TC-14 — Параллельная нагрузка (стресс)
```bash
# нужен siege; либо аналог через xargs+curl
siege -b -t10s "$BASE/" 2>/dev/null | tail -n 20    # availability должно быть 100%
curl -s -o /dev/null -w "после стресса -> %{http_code}\n" "$BASE/"   # сервер всё ещё отвечает 200
```
> Почему: проверяем, что нет утечек fd/памяти (ASan не сработал) и сервер обслуживает много соединений.

### TC-15 — Официальные тестеры 42
```bash
./tester        # общий тестер webserv (запускать на conf/tester.conf)
./cgi_tester    # тестер CGI
```
> Прогнать и сверить вывод. Это бинари из репозитория, на которые рассчитана `conf/tester.conf` и каталог
> `YoupiBanane/`.

---

## Итоговый лист защиты

Отметь результат каждого пункта. Бонусы засчитываются только при 100% mandatory.

| TC | Категория | M/B | Ожидаемое | Пройдено (✅/❌) |
|---|---|---|---|---|
| build | Компиляция без варнингов, ASan | M | 0 warnings | |
| TC-01 | GET статики | M | 200 | |
| TC-02 | 404 | M | 404 | |
| TC-03 | Редирект каталога | M | 301 → 200 | |
| TC-04 | Метод не разрешён | M | 405 | |
| TC-05 | `client_max_body_size` | M | 413 | |
| TC-06 | CGI (py + sh) | M/B | вывод скриптов | |
| TC-07 | Cookies/session | B | счётчик растёт | |
| TC-08 | Upload | M | 2xx + файл | |
| TC-09 | DELETE | M | 2xx + удалён | |
| TC-10 | Autoindex | M | HTML-листинг | |
| TC-11 | Path traversal | M | 4xx | |
| TC-12 | Несколько портов | M | оба отвечают | |
| TC-13 | Битый запрос | M | сервер жив | |
| TC-14 | Стресс | M | 100% availability | |
| TC-15 | Официальные тестеры | M | passed | |

> Для каждого кейса понимание «почему так» важнее самого результата: на защите автор должен показать **строку кода**,
> которая отвечает за наблюдаемое поведение. Ссылки в колонке «Почему» ведут ровно туда.
