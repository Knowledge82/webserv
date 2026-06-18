# 02 — Defense checklist and test cases

Reproducible checks for review. Each case is laid out the same way:
**what we check → command → expected → why (link to code)**.

Some checks are already automated in [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) — you can borrow
the ready-made curl assertions. The `./tester` and `./cgi_tester` binaries in the root are the official 42 testers.

## Test bench setup

We set variables up front so the commands are copy-pasteable and host-independent:

```bash
# --- shared bench variables ---
HOST=127.0.0.1
PORT=8080
BASE="http://$HOST:$PORT"
CONF=conf/tester.conf

# build with the sanitizer
make re

# start the server for all tests below
./webserv "$CONF" &
SRV_PID=$!
sleep 1   # let the sockets come up
```

At the very end of all tests: `kill "$SRV_PID"`.

---

## Build and basic hygiene

| Check | Command | Expected |
|---|---|---|
| Compiles without warnings | `make re` | binary `webserv`, **0 warnings** with `-Wall -Wextra -Werror -std=c++98` |
| No relink | `make && make` | the second call rebuilds nothing |
| Config check | `./webserv --check-config conf/tester.conf` | `OK: conf/tester.conf`, exit code 0 |
| Broken config doesn't crash | `./webserv conf/NO_SUCH_FILE` | a sane `Fatal: ...` error, exit 1, no segfault |

> ASan is enabled in `Makefile:27`. Any leak/heap-overflow during tests crashes the process with a report —
> that is exactly the "server must not crash / leak" requirement check.

---

## Functional test cases

### TC-01 — Basic GET (mandatory)
```bash
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/"        # expect: 200
```
> Why: `Connection::onReadable` → `selectLocation` finds `location /` → `FilesystemHandler::buildFileSystemReply`
> serves `www/index.html` (`src/FilesystemHandler.cpp:28`). `000` — server didn't start; `500` — no root set.

### TC-02 — Missing path → 404
```bash
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/notfound"  # expect: 404
```
> Why: `Fs::classifyPath` returns `PATH_MISSING` → `pathKindToHttpStatus` = 404 (`src/Filesystem.cpp:99`).

### TC-03 — Directory without trailing slash → 301
```bash
curl -s -o /dev/null -D - "$BASE/directory" | grep -i '^location'   # expect: Location: /directory/
```
> Why: `Connection::tryRedirectToSlashLocation` (`src/Connection.cpp:288`) returns `301` to `uri + "/"`,
> so relative links inside the directory don't break. Then `GET /directory/` must give `200`.

### TC-04 — Disallowed method → 405
```bash
curl -s -o /dev/null -w "%{http_code}\n" -X POST --data x "$BASE/"   # expect: 405
```
> Why: `location /` in `conf/tester.conf:8` allows only `GET`; `isAllowedMethod` (`src/Connection.cpp:180`)
> returns false → `buildErrorResponse(405)`.
>
> ⚠️ **Important nuance for the defense:** the method check deliberately **skips `DELETE`**
> (`if (method != "DELETE" && !isAllowedMethod(...))`, `src/Connection.cpp:622`) — a "test hack".
> So `curl -X DELETE "$BASE/"` gives 403 (from `handleDelete`, since you can't delete a directory), not 405.
> To see a real 405, use a method other than DELETE — e.g. `POST`. Good defense question:
> "why does DELETE not respect `allow_methods`?".

### TC-05 — `client_max_body_size` → 413 (body-size variable)
```bash
BODY_LEN=200                                              # limit for /post_body = 100 (tester.conf:13)
PAYLOAD=$(head -c "$BODY_LEN" /dev/zero | tr '\0' 'A')
curl -s -o /dev/null -w "%{http_code}\n" -X POST --data "$PAYLOAD" "$BASE/post_body"   # expect: 413
```
> Why: the `eff.clientMaxBodySize` check in `Connection::onReadable` (`src/Connection.cpp:576` and `:604`) → 413.
> Control: with `BODY_LEN=50` (below the limit) the same request gives `200` and body `post_body ok`.

### TC-06 — CGI, multi-interpreter (bonus)
```bash
curl -s "$BASE/cgi-bin/test.py"     # expect: <h1>Hello from Python!</h1>
curl -s "$BASE/cgi-bin/test.sh"     # expect: <h1>Hello from Bash!</h1>
```
> Why: `Http::isCgiRequest` matches the extension (`src/CgiHandler.cpp:264`) → `startCgi` forks and calls
> `execve` of the interpreter from `cgi .py ...` / `cgi .sh ...` (`conf/tester.conf:27-28`) → `parseCgiOutput`
> parses the CGI headers. Make sure the interpreter from the config exists in the environment
> (`/opt/pyenv/shims/python3`, `/bin/bash`); otherwise the script fails and the server returns `500`
> (without hanging — that's a check too).

### TC-07 — Cookies / session (bonus, cookie-jar)
```bash
JAR=$(mktemp)
curl -s -c "$JAR" "$BASE/session" >/dev/null   # 1st visit: server sets Set-Cookie session_id
curl -s -b "$JAR" "$BASE/session"              # 2nd visit: cookie is read, visit counter grows
```
> Why: `HttpRequest::getCookieValue("session_id")` + `HttpResponse::buildResponseWithCookie`
> (`src/HttpResponse.cpp:136`) in the `/session` branch inside `Connection::onReadable`.

### TC-08 — File upload (mandatory)
```bash
./webserv conf/upload.conf &  UP_PID=$!   ; sleep 1
curl -s -o /dev/null -w "%{http_code}\n" -X POST --data "hello upload" "$BASE/uploads"   # expect: 2xx
ls -l www/uploads/                                                                       # file appeared
kill "$UP_PID"
```
> Why: `location /uploads` with `upload_dir ./www/uploads` (`conf/upload.conf`) → `Connection::handleUpload`
> (`src/Connection.cpp:405`).

### TC-09 — File DELETE (mandatory)
```bash
echo tmp > www/uploads/victim.txt
curl -s -o /dev/null -w "%{http_code}\n" -X DELETE "$BASE/uploads/victim.txt"   # expect: 2xx
test -f www/uploads/victim.txt && echo "FAIL: file still there" || echo "OK: deleted"
```
> Why: `Connection::handleDelete` (`src/Connection.cpp:337`) maps the URI to a path via `safeJoin`
> and removes the file. Works directly on `conf/tester.conf` (DELETE bypasses the `allow_methods` check, see TC-04).
> Deleting an existing file gives `200`; deleting a directory — `403`; a missing one — `404`.

### TC-10 — Autoindex (mandatory)
```bash
./webserv conf/autoindex.conf &  AI_PID=$!  ; sleep 1
curl -s "$BASE/docs/" | grep -i '<a href'    # expect: an HTML listing of www/docs/
kill "$AI_PID"
```
> Why: `location /docs/` with `autoindex on` → `Http::appendDirectoryListingHtml` (`src/Autoindex.cpp`).
> Control: `GET /` (where `autoindex off`) must **not** produce a listing.

### TC-11 — Path traversal is blocked (security)
```bash
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/../../etc/passwd"          # expect: 4xx (not 200 with content!)
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/%2e%2e/%2e%2e/etc/passwd"  # encoded .. is blocked too
```
> Why: `Http::safeJoin` normalizes `.`/`..` segment by segment and returns `403` if `..` escapes the root
> (`src/Path.cpp:185-195`). Encoded `%2e%2e` is decoded to `..` **before** the check.

### TC-12 — Multiple servers / ports (mandatory)
```bash
./webserv conf/2serv.conf &  MS_PID=$!  ; sleep 1
curl -s -o /dev/null -w "8080 -> %{http_code}\n" "http://$HOST:8080/"
curl -s -o /dev/null -w "8081 -> %{http_code}\n" "http://$HOST:8081/"
kill "$MS_PID"
```
> Why: `setupListenSockets` opens one socket per `listen` of every `server{}` block.
> Both ports must **respond** (the code may be 404/500 if `./www1`/`./www2` are missing — what matters is that
> the server listens on both ports and does not crash).

### TC-13 — Resistance to a "broken" request (server doesn't hang)
```bash
printf 'GET / HTTP/1.1\r\n' | nc -w1 "$HOST" "$PORT"   # incomplete request, no terminating \r\n\r\n
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/"      # server alive → 200 again
```
> Why: `HttpRequest::parse` stays in the `HEADERS` state and waits for data without blocking the event loop
> (`src/HttpRequest.cpp:140`). A close/timeout does not bring down `Server::run`.

### TC-14 — Concurrent load (stress)
```bash
# needs siege; or an equivalent via xargs+curl
siege -b -t10s "$BASE/" 2>/dev/null | tail -n 20    # availability should be 100%
curl -s -o /dev/null -w "after stress -> %{http_code}\n" "$BASE/"   # server still answers 200
```
> Why: we verify there are no fd/memory leaks (ASan didn't fire) and the server serves many connections.

### TC-15 — Official 42 testers
```bash
./tester        # general webserv tester (run on conf/tester.conf)
./cgi_tester    # CGI tester
```
> Run them and compare the output. These are repo binaries that `conf/tester.conf` and the `YoupiBanane/`
> directory are designed for.

---

## Final defense sheet

Mark each item. Bonuses only count with 100% mandatory.

| TC | Category | M/B | Expected | Passed (✅/❌) |
|---|---|---|---|---|
| build | Compiles, no warnings, ASan | M | 0 warnings | |
| TC-01 | GET static | M | 200 | |
| TC-02 | 404 | M | 404 | |
| TC-03 | Directory redirect | M | 301 → 200 | |
| TC-04 | Method not allowed | M | 405 | |
| TC-05 | `client_max_body_size` | M | 413 | |
| TC-06 | CGI (py + sh) | M/B | script output | |
| TC-07 | Cookies/session | B | counter grows | |
| TC-08 | Upload | M | 2xx + file | |
| TC-09 | DELETE | M | 2xx + removed | |
| TC-10 | Autoindex | M | HTML listing | |
| TC-11 | Path traversal | M | 4xx | |
| TC-12 | Multiple ports | M | both respond | |
| TC-13 | Broken request | M | server alive | |
| TC-14 | Stress | M | 100% availability | |
| TC-15 | Official testers | M | passed | |
