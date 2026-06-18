# 03 — Architecture and the end-to-end request flow

## Repository layout

```
webserv/
├── Makefile                 # build (C++98, -Wall -Wextra -Werror, ASan)
├── include/                 # headers (.hpp)
├── src/                     # implementation (.cpp)
├── conf/                    # example configs (tester, upload, delete, 2serv, autoindex…)
├── www/                     # static root (index.html, cgi-bin/, uploads/, docs/)
├── YoupiBanane/             # data for the official ./tester
├── tester, cgi_tester       # official 42 test binaries
├── .github/workflows/ci.yml # CI: build + curl smoke tests
└── docs/ , docs-en/         # ← this guide (RU / EN)
```

The code splits into 4 logical layers:

| Layer | Modules | Responsibility |
|---|---|---|
| **Config** | `ConfigLoader`, `Tokenizer`, `ConfigParser`, `Config`, `EffectiveConfig` | read the config → tree of structs → merged rules per request |
| **Transport / event loop** | `Server` | sockets, `poll()`, dispatching events per fd |
| **Connection state** | `Connection` | client state machine, routing, handler selection, CGI |
| **HTTP logic** | `HttpRequest`, `HttpReply`, `HttpResponse`, `FilesystemHandler`, `Path`, `Filesystem`, `Autoindex`, `Mime`, `CgiHandler` | request parsing, building and serializing the response, files, CGI |

Helpers: `Log` (logging macros), `FdUtils` (`createListenSocket`, `setNonBlocking`).

## Module dependency scheme

```mermaid
flowchart TD
    main[main.cpp] --> CL[ConfigLoader]
    CL --> CP[ConfigParser] --> TZ[Tokenizer]
    CP --> CFG[(Config structs)]
    main --> SRV[Server: poll loop]
    SRV --> CONN[Connection: state machine]
    CONN --> REQ[HttpRequest: parser]
    CONN --> ROUTE{Routing:<br/>selectLocation +<br/>buildEffectiveConfig}
    ROUTE --> EFF[(EffectiveConfig)]
    CONN --> FH[FilesystemHandler]
    FH --> PATH[Path: safeJoin]
    FH --> FS[Filesystem: classify/read]
    FH --> AI[Autoindex]
    FH --> MIME[Mime]
    CONN --> CGI[CgiHandler]
    CONN --> RESP[HttpResponse: bytes]
    FH --> REPLY[(HttpReply model)]
    REPLY --> RESP
```

## End-to-end flow of one request (from socket to response)

```mermaid
flowchart TD
    A[Client: TCP connect + HTTP request] --> B[Server::run: poll]
    B -->|POLLIN on listen fd| C[acceptPendingConnections<br/>new Connection, READING]
    B -->|POLLIN on client fd| D[handleClientEvent → Connection::onReadable]
    D --> E[recv into in_ → HttpRequest::parse]
    E -->|HEADERS / BODY| D2[wait for more bytes, stay READING]
    E -->|ERROR| ERR[buildErrorResponse 4xx → WRITING]
    E -->|COMPLETE| F[Routing: selectLocation + buildEffectiveConfig]
    F --> G{What to do?}
    G -->|redirect| R1[buildRedirectResponse → WRITING]
    G -->|method not allowed| R2[405 → WRITING]
    G -->|DELETE| H1[handleDelete → WRITING]
    G -->|POST/PUT + upload_dir| H2[handleUpload → WRITING]
    G -->|extension = CGI| H3[startCgi: fork/execve → state=CGI]
    G -->|static| H4[buildFileSystemReply → WRITING]
    H3 -->|onCgiEvent: stdin/stdout pipes| H3b[parseCgiOutput → WRITING]
    R1 & R2 & H1 & H2 & H4 & H3b & ERR --> W[Server: POLLOUT → Connection::onWritable]
    W --> S[send out_ / stream file from disk]
    S -->|all sent| CL[closeConnection / ready for next request]
```

### Key invariants (what to look at during review)

1. **A single `poll()`** for the whole server — `src/Server.cpp:404`. No `read`/`write` outside a `poll` reaction.
2. **`pollFds_[i]` and `fdEntries_[i]` are index-synchronized** — the arrays are rebuilt together in `buildPollFds`.
3. **`break` when a client closes** — if a connection closes inside an iteration, the loop over `pollFds_` breaks,
   because the arrays become invalid (`src/Server.cpp:432`).
4. **`errno` is not inspected after I/O** — a 42 subject rule; any `< 0` = stop the operation,
   `poll` will sort it out later (`src/Server.cpp:367`).

## Where to find what (module → files map)

| Module | Header | Implementation | Doc |
|---|---|---|---|
| Config | `include/Config*.hpp`, `EffectiveConfig.hpp` | `src/Config*.cpp`, `EffectiveConfig.cpp` | [`04`](04-config.md) |
| Server | `include/Server.hpp` | `src/Server.cpp` | [`05`](05-server-eventloop.md) |
| Connection | `include/Connection.hpp` | `src/Connection.cpp` | [`06`](06-connection.md) |
| HttpRequest | `include/HttpRequest.hpp` | `src/HttpRequest.cpp` | [`07`](07-http-request.md) |
| HttpReply / HttpResponse | `include/HttpReply.hpp`, `HttpResponse.hpp` | `src/HttpResponse.cpp` | [`08`](08-http-response.md) |
| Static | `Path/Filesystem/FilesystemHandler/Autoindex/Mime` | same-named `.cpp` | [`09`](09-static-files.md) |
| CGI | `include/CgiHandler.hpp` | `src/CgiHandler.cpp` + `Connection` CGI branch | [`10`](10-cgi.md) |
