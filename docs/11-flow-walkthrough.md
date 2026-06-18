# 11 — Module breakdown and program flow

This file is a full end-to-end walkthrough of webserv: how a request travels through the modules from process startup to bytes in the client socket. It is useful both for understanding the architecture and for defense preparation (the end includes a reviewer Q&A sheet for 42).

## Global flow map (4 stages)

```
[STAGE 1: Creation and Parsing]
main.cpp ──> ConfigLoader ──> ConfigTokenizer ──> ConfigParser ──> Config / structures

[STAGE 2: Core Initialization]
Server (constructor) ──> createListenSocket: socket → setsockopt(SO_REUSEADDR)
                          → setNonBlocking → bind → listen ──> master sockets + pollfd

[STAGE 3: Lifecycle (the reactor loop — the “meat” of the server)]
Server::run() ──> poll()  ──┬──> new connection?  ──> accept() ──> new Connection
                            └──> client activity?  ──> Connection::onReadable()

[STAGE 4: HTTP pipeline and response generation]
onReadable ──> HttpRequest::parse()                 (parsing)
           ──> selectLocation + EffectiveConfig     (routing/matching)
           ──> ┌ static  ──> FilesystemHandler / Autoindex / streaming
               ├ methods  ──> handleDelete / handleUpload
               └ dynamic  ──> startCgi ──> onCgiEvent (pipes, non-blocking waitpid)
           ──> Connection::onWritable() ──> send() to the client
```

```mermaid
flowchart TD
    M[main.cpp] --> L[ConfigLoader] --> TK[ConfigTokenizer] --> PP[ConfigParser] --> CFG[(Config)]
    CFG --> SRV[Server: socket/bind/listen]
    SRV --> RUN[Server::run: poll loop]
    RUN -->|FD_LISTEN| ACC[accept → new Connection]
    RUN -->|FD_CLIENT| RD[Connection::onReadable]
    RD --> PARSE[HttpRequest::parse]
    PARSE --> ROUTE[selectLocation + buildEffectiveConfig]
    ROUTE --> STAT[static: FilesystemHandler/Autoindex]
    ROUTE --> UPD[handleUpload / handleDelete]
    ROUTE --> CGI[startCgi → onCgiEvent]
    STAT & UPD & CGI --> WR[Connection::onWritable → send]
```

---

## Stage 1 — Creation and parsing

`main.cpp` → `ConfigLoader` → `ConfigTokenizer` (lexemes) → `ConfigParser` (grammar) → `Config` (tree of `ServerConfig`/`LocationConfig`/`ListenConfig` structures).

**How it works.** Depending on the number of arguments, the program chooses the config source; the file path is parsed by the tokenizer and parser, and the default config is built in memory. The configuration layer is described in detail in [`04-config.md`](04-config.md).

**Snippet** (`src/main.cpp:34-57`):

```cpp
Config cfg;
if (argc == 1)
    cfg = ConfigLoader::loadDefault();             // default server{} on :8080
else if (argc == 2)
{
    if (std::string(argv) == "--check-config")  // only validate syntax and exit [ppl-ai-file-upload.s3.amazonaws](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/46164348/020a38a2-0c8c-4128-801d-799411daa30c/10-cgi.md?AWSAccessKeyId=ASIA2F3EMEYE4RIZ77MC&Signature=qOTHsY8MB4roAu7Udw86BjQvQUA%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEOX%2F%2F%2F%2F%2F%2F%2F%2F%2F%2FwEaCXVzLWVhc3QtMSJHMEUCIDeMWvOXjHr4BbygiMt3msU6yoLMJw8rl7iAdnlcVylIAiEA7Fuxgk3CNkuSDR5ZafDCXUi08Zw%2B2I60hd1%2FEyN6Ug4q%2FAQIrv%2F%2F%2F%2F%2F%2F%2F%2F%2F%2FARABGgw2OTk3NTMzMDk3MDUiDA9Sb5khlvSZBTlf2CrQBIkctZPk7San7e927F4TQKQdBn6erAm%2BNIiD%2FwopavR3HLcfgnV4fyBtXewp%2FQ6SZAi9%2FpE8izuFntEpWerqFCRO1KxKdI4PIBEH6CNIUP7kxz3E4Jr80gr1CpILymapS%2FVSeETIwvbMGYwUS2b1c1bxEALOXmBFiEQmMbw1FweFYAw86TvQJVR6iMm%2BzRcBlU9M0a8TAC2VONZPChQTnHxkFL2KL1L5O8Zbc8AdK6NoSDfgIfCJ2RS4DDy%2FoEBWWTYMZ8EhKhxbC5MFubkpWM6ALXVeVIdssOwp1M6aAqAJH0P1BJy5wFGn4USv0tS5EDEUbsNwYqTOyolsmUA4HlYMJZDX839oW0o4kIMZAU89fdBzpXdiCvQ01ZbUOJ%2BTACXxSfzhMLOOQw9oGOcUJGynMpTWZiiuqvbxynKvEmHRFZLowX6ccPKli2JHh2HPQYqTkKJ8QFLATirFGtzMUM5x%2FQgShDUYgc0jruyYoKNlJxSNXcOxERgnvLmN1zRzyQQm4diko9HXIFeUqgFoFMmaUFgDEirMs3oCG%2FbbUJVRk9fRbg3xSbztz2dAUqVtW1P9l%2B5BzsB0xMduCS0iN2PHkSJJHxCiQYGIsXNyY7AeoSlKDwRVoFywm8vaU2S021fewRceRD4iC0FegU3ZDMRKZ3clL9FZpRPBSYR6gFMvtNtyJr0VFfKyASGm7E5XnnuJzArRt27is6lwow3AQ32FdDHpr0UEI%2BubyRaAwIVVi%2Bhe1qNU2DOmJvDEZJ7rgCvloDye5eUITc2tTfwtSoUwi7XR0QY6mAHstBYF5P5d4GwCFUNXkC7TW4KPL9Px%2BSPqzaCiyL1I7hlZKf85iF2D7ogtRwVXeqoSSSYvyXjG5lxZw8vC9mZkaa9Wm5p0ePq0fiAkf%2F7aSMwFJJvpQTU7z3rquWnVANLskMkPMMSdWi%2F9VQVm8XhLaREe11nCTurtnw%2Fetx1ridrhBotvYyu4nOpQhQ4QVyTiVbEqfY5l7Q%3D%3D&Expires=1781819486)
    { cfg = ConfigLoader::loadDefault(); std::cout << "OK: default config\n"; return 0; }
    cfg = ConfigLoader::loadFromFile(argv);     // Tokenizer + Parser → Config [ppl-ai-file-upload.s3.amazonaws](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/46164348/020a38a2-0c8c-4128-801d-799411daa30c/10-cgi.md?AWSAccessKeyId=ASIA2F3EMEYE4RIZ77MC&Signature=qOTHsY8MB4roAu7Udw86BjQvQUA%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEOX%2F%2F%2F%2F%2F%2F%2F%2F%2F%2FwEaCXVzLWVhc3QtMSJHMEUCIDeMWvOXjHr4BbygiMt3msU6yoLMJw8rl7iAdnlcVylIAiEA7Fuxgk3CNkuSDR5ZafDCXUi08Zw%2B2I60hd1%2FEyN6Ug4q%2FAQIrv%2F%2F%2F%2F%2F%2F%2F%2F%2F%2FARABGgw2OTk3NTMzMDk3MDUiDA9Sb5khlvSZBTlf2CrQBIkctZPk7San7e927F4TQKQdBn6erAm%2BNIiD%2FwopavR3HLcfgnV4fyBtXewp%2FQ6SZAi9%2FpE8izuFntEpWerqFCRO1KxKdI4PIBEH6CNIUP7kxz3E4Jr80gr1CpILymapS%2FVSeETIwvbMGYwUS2b1c1bxEALOXmBFiEQmMbw1FweFYAw86TvQJVR6iMm%2BzRcBlU9M0a8TAC2VONZPChQTnHxkFL2KL1L5O8Zbc8AdK6NoSDfgIfCJ2RS4DDy%2FoEBWWTYMZ8EhKhxbC5MFubkpWM6ALXVeVIdssOwp1M6aAqAJH0P1BJy5wFGn4USv0tS5EDEUbsNwYqTOyolsmUA4HlYMJZDX839oW0o4kIMZAU89fdBzpXdiCvQ01ZbUOJ%2BTACXxSfzhMLOOQw9oGOcUJGynMpTWZiiuqvbxynKvEmHRFZLowX6ccPKli2JHh2HPQYqTkKJ8QFLATirFGtzMUM5x%2FQgShDUYgc0jruyYoKNlJxSNXcOxERgnvLmN1zRzyQQm4diko9HXIFeUqgFoFMmaUFgDEirMs3oCG%2FbbUJVRk9fRbg3xSbztz2dAUqVtW1P9l%2B5BzsB0xMduCS0iN2PHkSJJHxCiQYGIsXNyY7AeoSlKDwRVoFywm8vaU2S021fewRceRD4iC0FegU3ZDMRKZ3clL9FZpRPBSYR6gFMvtNtyJr0VFfKyASGm7E5XnnuJzArRt27is6lwow3AQ32FdDHpr0UEI%2BubyRaAwIVVi%2Bhe1qNU2DOmJvDEZJ7rgCvloDye5eUITc2tTfwtSoUwi7XR0QY6mAHstBYF5P5d4GwCFUNXkC7TW4KPL9Px%2BSPqzaCiyL1I7hlZKf85iF2D7ogtRwVXeqoSSSYvyXjG5lxZw8vC9mZkaa9Wm5p0ePq0fiAkf%2F7aSMwFJJvpQTU7z3rquWnVANLskMkPMMSdWi%2F9VQVm8XhLaREe11nCTurtnw%2Fetx1ridrhBotvYyu4nOpQhQ4QVyTiVbEqfY5l7Q%3D%3D&Expires=1781819486)
}
// ...
Server s(cfg);   // ← STAGE 2
s.run();         // ← STAGE 3
```

**Validation example.**

```bash
./webserv --check-config conf/tester.conf   # syntax OK → "OK: ...", exit 0
./webserv conf/broken.conf                  # config error → "Fatal: ...", exit 1 (does not crash)
```

---

## Stage 2 — Core initialization

For every `host:port` pair in the config, the server creates a **listening master socket**. The classic steps are:
`socket → setsockopt(SO_REUSEADDR) → setNonBlocking → bind → listen`.

**How it works.** `createListenSocket` creates a non-blocking TCP socket, enables address reuse (so it does not have to wait for `TIME_WAIT` after restart), binds it to the address, and starts listening. All listening fds are then placed into the stage 3 `pollfd` array.

**Snippet** (`src/Server.cpp:48-114`, shortened):

```cpp
int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);          // TCP/IPv4
if (listenFd < 0) throw std::runtime_error("socket failed");

int yes = 1;
::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));  // quick restart
setNonBlocking(listenFd);                                   // ← key to the non-blocking model

struct sockaddr_in addr = {};
addr.sin_family = AF_INET;
addr.sin_port   = htons(static_cast<unsigned short>(port)); // host→network byte order
::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

if (::bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    throw std::runtime_error("bind failed");
if (::listen(listenFd, 128) < 0)                            // backlog queue = 128
    throw std::runtime_error("listen failed");
```

**Validation example.**

```bash
./webserv conf/2serv.conf &                  # two server{} blocks with different ports
ss -tlnp | grep -E ':8080|:8081'             # both ports in LISTEN
# duplicate port in the config should fail at startup:
./webserv conf/dup_port.conf                 # bind failed → "Fatal: ...", exit 1
```

---

## Stage 3 — Lifecycle (reactor loop)

The heart of the server is the **single** `poll()` in `Server::run()`. It watches read and write readiness for all fds at once (listening sockets, client sockets, CGI pipes) and dispatches events to handlers.

**How it works.** On each iteration, `buildPollFds()` builds the current `pollfd` array (each `Connection` reports which events it is interested in), then one `::poll()` waits up to 1 second. Based on `revents`, the event is dispatched by fd type: `FD_LISTEN` → `accept`, `FD_CLIENT` → `onReadable/onWritable`, otherwise → CGI pipe. When a client is closed, the loop breaks because `pollFds_`/`fdEntries_` must be rebuilt.

**Snippet — loop** (`src/Server.cpp:392-438`, shortened):

```cpp
while (true)
{
    buildPollFds();                                       // pollFds_[i] ↔ fdEntries_[i]
    if (pollFds_.empty()) continue;

    int eventCount = ::poll(&pollFds_, pollFds_.size(), 1000);  // THE only poll
    if (eventCount <= 0) continue;

    for (size_t i = 0; i < pollFds_.size(); ++i)
    {
        if (pollFds_[i].revents == 0) continue;
        const FdEntry &e = fdEntries_[i];
        short re = pollFds_[i].revents;

        if      (e.kind == FD_LISTEN) handleListenEvent(e, re);     // accept()
        else if (e.kind == FD_CLIENT) clientClosed = handleClientEvent(e, re);
        else                          clientClosed = handleCgiEvent(e, re);

        if (clientClosed) break;     // fdEntries_ invalidated → rebuild next iteration
    }
}
```

**Snippet — accept** (`src/Server.cpp:351-389`, shortened):

```cpp
while (true)                                          // accept everyone queued in one poll
{
    int clientFd = ::accept(listenFd, ...);
    if (clientFd < 0)                                 // 42 rule: do NOT inspect errno —
        return;                                       // just return, poll will wake us again
    setNonBlocking(clientFd);
    connections_.insert(std::make_pair(clientFd, Connection(clientFd, &cfg_, serverIndex)));
}
```

**Validation example.**

```bash
# parallel clients are handled by one poll, the server does not block:
curl -s "$BASE/" & curl -s "$BASE/" & curl -s "$BASE/" & wait
```

---

## Stage 4 — HTTP pipeline and response generation

When there is data on the client fd, `Connection::onReadable` reads it, parses the request, and — once the request is complete — routes it into one of the branches. The response then goes through `onWritable → send`.

**How it works.** `recv` → `HttpRequest::parse` (incrementally, see [`07-http-request.md`](07-http-request.md)) → when `COMPLETE`, the server selects the location (`selectLocation`) and merges the effective config (`buildEffectiveConfig`) → strict order of checks: body limit (413) → redirect → method (405) → DELETE / upload / CGI / static content. Each branch stores a response in `out_` and moves the connection to `WRITING` (or `CGI`).

**Routing snippet** (`src/Connection.cpp:520-655`, shortened):

```cpp
ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
if (n <= 0) return false;                                   // 0/<0 → Server will close the connection
in_.append(buf, n);

HttpRequest::State st = request_.parse(in_, maxHeaderBytes, maxBodyBytes);
if (st == HttpRequest::ERROR)    { out_ = HttpResponse::buildErrorResponse(...); state_ = WRITING; return true; }
if (st != HttpRequest::COMPLETE) return true;              // not complete yet — stay READING

const LocationConfig *loc = selectLocation(srv.locations, uri);   // longest prefix
EffectiveConfig       eff = buildEffectiveConfig(srv, loc);       // server → location

if (eff.hasClientMaxBodySize && request_.getContentLength() > eff.clientMaxBodySize)
    { out_ = HttpResponse::buildErrorResponse(413); state_ = WRITING; return true; }
if (eff.hasRedirect)                 { out_ = HttpResponse::buildRedirectResponse(...); state_ = WRITING; return true; }
if (!isAllowedMethod(method, eff))   { out_ = HttpResponse::buildErrorResponse(405); state_ = WRITING; return true; }

if (method == "DELETE")              return handleDelete(eff);            // src/Connection.cpp:337
if ((method=="POST"||method=="PUT") && loc && loc->hasUploadDir)
                                     return handleUpload(eff, loc);       // src/Connection.cpp:405
if (Http::isCgiRequest(loc, uri))    { startCgi(eff, loc, request_); return true; }  // → state CGI
Http::HttpReply rep = Http::buildFileSystemReply(eff, loc, uri);          // static content
return prepareReply(rep);
```

**Sending snippet** (`src/Connection.cpp:786`, two phases):

```cpp
if (!out_.empty()) {                            // phase 1: headers/small response from buffer
    ssize_t n = ::send(fd_, out_.c_str(), out_.size(), 0);
    if (n <= 0) return false;
    out_.erase(0, n);
    if (!out_.empty()) return true;             // partial write — wait for next POLLOUT
}
if (fileStreamFd_ >= 0) { /* phase 2: stream large files in 8KB chunks, not kept in memory */ }
```

**Validation example.**

```bash
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/"                 # 200 (static)
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/notfound"         # 404
curl -s -o /dev/null -w "%{http_code}\n" -X POST --data x "$BASE/" # 405 (location / is GET only)
curl -s "$BASE/cgi-bin/test.py"                                   # CGI output, 200
```

---

## Reviewer Q&A and code references

Below are the questions from the official evaluation sheet, together with short answers and code references. This is the “point to the code” cheat sheet for the defense.

## Siege installation and basic questions

```bash
# Install siege (for the stress test, see the “Siege” section below)
sudo apt-get install -y siege      # Debian/Ubuntu
brew install siege                 # macOS
```

**Q: Explain the basics of an HTTP server.**  
A: The server listens on a TCP port, accepts connections, reads an HTTP request (request line + headers + body), routes by URI/method, builds an HTTP response (status + headers + body), and sends it back. See [`../README.md`](../README.md) and [`03-architecture.md`](03-architecture.md) for the broader picture.

**Q: Which I/O multiplexing function does the project use?**  
A: `poll()` — `src/Server.cpp:404`. (`select/poll/epoll/kqueue` would be acceptable; this implementation uses `poll`.)

**Q: How does `select()`/the equivalent work?**  
A: `poll()` takes an array of `pollfd` entries (fd + desired events `POLLIN/POLLOUT`), blocks until at least one fd is ready or the timeout expires, and marks ready fds in `revents`. We only process ready fds, so we do not block on any single socket.

**Q: Is there only one `poll()`? How does the server accept and read/write clients at the same time?**  
A: Yes, there is **one single** `poll()` in the main `Server::run()` loop (`src/Server.cpp:404`). The same array contains listening sockets, client sockets, and CGI pipes. `buildPollFds()` sets `POLLIN` when a connection should read and `POLLOUT` when it has data to send — `wantedPollEvents()`, `src/Connection.cpp:273`. So reading and writing are checked **simultaneously** in one `poll`.

> ⚠️ Zero-point criterion: if `poll` is not in the main loop or does not check read and write simultaneously. Here it does (`POLLIN|POLLOUT` in one array).

**Q: Is there no more than one read/write per client per `poll()`?**  
A: Yes. For one event, `onReadable` does one `::recv` (`src/Connection.cpp:525`), and `onWritable` does one `::send` (`src/Connection.cpp:786`). There are no “read until EOF” loops on the socket.

**Q: On read/recv/write/send errors, is the client removed?**  
A: Yes. `onReadable`/`onWritable` return `false` when `n <= 0`, and `Server` closes the connection (`closeConnection`, `src/Server.cpp:327`).

**Q: Is the return value checked, including both 0 and -1?**  
A: Yes: `if (n == 0) return false;` (client closed) and `if (n < 0) return false;` (error) — `src/Connection.cpp:526-532`. Both cases are handled.

**Q: Is `errno` checked after read/write?**  
A: **No** — that is a subject rule (`errno` after I/O = zero points). Any `< 0` simply ends the operation, and we trust `poll`. See the comment in `acceptPendingConnections` (`src/Server.cpp:367-371`).

**Q: Is there any I/O outside `poll()`?**  
A: On sockets/pipes — no, everything goes through `poll`. File I/O from disk (`read`/`open` for static files and uploads) is not socket I/O; large files are streamed in chunks in `onWritable` under `POLLOUT`.

**Q: Does the project compile without re-link issues?**  
A: `make re` builds with `-Wall -Wextra -Werror -std=c++98 -fsanitize=address` without warnings; running `make` again should not trigger unnecessary recompilation/re-linking if the Makefile dependencies are correct.

## Configuration

| What to check | Command / config | Where in code |
|---|---|---|
| Multiple servers on different ports | `conf/2serv.conf`; `curl :8080/`, `curl :8081/` | `ServerConfig.listens`, `Server::run` |
| Different hostnames | `curl --resolve example.com:8080:127.0.0.1 http://example.com/` | `server_name` / server selection |
| Custom 404 page | `error_page 404 /404.html;` → `curl -i $BASE/nope` | `buildErrorResponse` |
| Body size limit | `curl -X POST -H "Content-Type: plain/text" --data "..." $BASE/post_body` → 413 | `Connection.cpp:604` |
| Routes into different directories | `root`/`alias` in `location` | `safeJoin`/`safeJoinAlias`, `Path.cpp:132` |
| Index file for a directory | `index index.html;` | `FilesystemHandler` |
| Allowed methods for a route | `allow_methods GET;` → DELETE with/without permission | `isAllowedMethod`, `Connection.cpp:180` |

> Reviewer note: status codes must be **correct**. Check `reasonPhrase`
> (`src/HttpResponse.cpp:26`) — it contains 200/301/403/404/405/413/500 and so on.

## Basic checks

```bash
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/"                  # GET   → 200
curl -s -o /dev/null -w "%{http_code}\n" -X POST --data x "$BASE/upload/a.txt"  # POST → 201
curl -s -o /dev/null -w "%{http_code}\n" -X DELETE "$BASE/upload/a.txt"         # DELETE → 200/204
printf 'WTF / HTTP/1.1\r\n\r\n' | nc -w1 "$HOST" "$PORT"           # UNKNOWN method → does NOT crash
curl -s -o up.txt "$BASE/upload/a.txt"                            # file downloaded back
```

Each test returns the correct status; an unknown request does not crash the server (`HttpRequest::parse` → 400/405, not a crash).

## Check CGI

```bash
curl -s "$BASE/cgi-bin/test.py"                       # GET-CGI → 200, script output
curl -s -X POST --data "name=42" "$BASE/cgi-bin/test.py"   # POST-CGI: body goes to stdin
```

- Correct working directory: before `execve`, the child process does `chdir(workDir)`
  (see [`10-cgi.md`](10-cgi.md), `startCgi`).
- GET and POST: CGI/1.1 environment variables, `CONTENT_LENGTH` for POST (`CgiHandler.cpp:131-135`).
- Errors / infinite loop: `cgiDeadline_` timeout (120s) → 504/500; script failure (`_exit(127)` / signal) is handled by `waitpid` → 500. **The server does not crash**, the client sees the error.

```bash
# infinite-loop script → the server survives and returns 504/500 on timeout
curl -s -o /dev/null -w "%{http_code}\n" "$BASE/cgi-bin/loop.py"
```

## Check with a browser

Open DevTools → Network and visit `$BASE/`:
- Request/Response headers are visible (`Content-Type`, `Content-Length`, `Connection: close`).
- The static site is served fully (html/css/png — correct `Content-Type`, `Mime.cpp`).
- Invalid URL → 404 page; directory → autoindex (if enabled); redirect URL → 301/302 with
  `Location`.

## Port issues

- Multiple ports + different sites in the browser — each port serves its own `root`.
- The same port listed twice in the config → startup fails (`bind failed`).
- Multiple instances sharing one port — the second `bind` on an occupied port will fail; if one
  configuration is broken, the server should not “magically” work on its port.

## Siege and stress test

```bash
siege -b -t30S "$BASE/"          # 30-second benchmark on the home page
```

- **Availability ≥ 99.5%** on a simple GET (`siege -b`).
- **No memory leaks**: watch process RSS — it should not grow forever
  (`ps -o rss= -p $(pgrep webserv)` in a loop; ASan also catches leaks).
- **No hanging connections**: after `siege`, there should be no idle `ESTABLISHED` sockets.
- `siege -b` should run **indefinitely** without restarting the server.

---

Back to the table of contents: [`README.md`](README.md).
