# 01 — Subject requirements (mandatory + bonus)

A structured retelling of the `webserv` requirements mapped to **"requirement → where in code"**.
The "Where in code" column is the place to start checking during a defense.

## General requirements (mandatory)

| # | Requirement | Where in code / how to check |
|---|---|---|
| 1 | **C++98**, compiled with `-Wall -Wextra -Werror` | `Makefile:27` (`-std=c++98`, + `-fsanitize=address -g3`) |
| 2 | Own `Makefile`, no relink (`all/clean/fclean/re`) | `Makefile:51-78` |
| 3 | Run: `./webserv [config]`; a default config exists | `src/main.cpp:30`, `ConfigLoader::loadDefault()` |
| 4 | The server **must never crash** under any circumstances | `try/catch` in `main.cpp:32`, careful fd closing |
| 5 | **One** `poll()` (or equivalent) for everything: read and write | `src/Server.cpp:404` (the only `::poll`) |
| 6 | `poll()` checks **read and write at the same time** | `pollfd.events` = `POLLIN\|POLLOUT` (`buildPollFds`, `wantedPollEvents`) |
| 7 | **Any** `read`/`recv`/`write`/`send` only after `poll`, and fd is closed on I/O error | `Connection::onReadable/onWritable`, no `errno` check after I/O (`Server.cpp:367`) |
| 8 | Non-blocking fds (sockets and CGI pipes) | `setNonBlocking()` in `acceptPendingConnections` and `startCgi` |
| 9 | Accurate **HTTP status codes** | `src/HttpResponse.cpp:26` (`reasonPhrase`) |
| 10 | **Default error pages** when none are configured | `HttpResponse::buildErrorResponse` |
| 11 | At least **GET, POST, DELETE** methods | `Connection::onReadable` (branches), `handleDelete`, `handleUpload` |
| 12 | Static file serving | `FilesystemHandler::buildFileSystemReply` |
| 13 | **Upload** of files by the client | `Connection::handleUpload` (`src/Connection.cpp:405`) |
| 14 | One server listening on multiple ports | `ServerConfig::listens` (a vector), `setupListenSockets` |
| 15 | Works with a real browser | checked manually (see `02-evaluation.md`) |
| 16 | Stress-resistant (does not hang under load) | event loop + `maxHeaderBytes/maxBodyBytes` limits |

## Configuration file (mandatory)

nginx-like format. Implemented directives (see parser `src/ConfigParser.cpp` and structs `include/Config.hpp`):

| Directive | Level | Struct field | Check in |
|---|---|---|---|
| `listen host:port` | server | `ServerConfig::listens` | `conf/tester.conf:2` |
| `root` | server/location | `*.root` (+`hasRoot`) | `conf/tester.conf:4` |
| `index` | server/location | `*.index` | `conf/tester.conf:5` |
| `autoindex on/off` | server/location | `*.autoindex` | `conf/autoindex.conf` |
| `client_max_body_size` | server/location | `*.clientMaxBodySize` | `conf/tester.conf:13` |
| `allow_methods` | location | `LocationConfig::allowedMethods` | `conf/tester.conf:8` |
| `alias` | location | `LocationConfig::alias` | `conf/tester.conf:18` |
| `cgi .ext /path` | location | `LocationConfig::cgiHandlers` | `conf/tester.conf:21,27` |
| `return` / redirect | location | `LocationConfig::redirect*` | `conf/delete.conf`, `conf/*` |
| `upload` dir | location | `LocationConfig::uploadDir` | `conf/upload.conf` |
| `error_page` | server | `ServerConfig::errorPages` | `include/Config.hpp:86` |

The key config idea is **inheritance**: a `hasX` + `X` pair distinguishes "not set" from "set to empty/false".
If a `location` doesn't set `root`, it inherits the server one (`buildEffectiveConfig`, see [`04-config.md`](04-config.md)).

## CGI (mandatory)

| Requirement | Where in code |
|---|---|
| Launch CGI by extension | `Http::isCgiRequest` (`src/CgiHandler.cpp:264`) |
| Pass request body to stdin, read stdout | `Connection::onCgiEvent` (`src/Connection.cpp:1077`) |
| Correct handling of relative paths (chdir into the script's dir) | `startCgi` → `::chdir(workDir)` (`src/Connection.cpp:1027`) |
| CGI environment variables (method, query, content-length…) | `prepareCgiArgs` (`src/CgiHandler.cpp:113-139`) |
| Server handles chunked/EOF itself correctly | parsing `Transfer-Encoding: chunked` + closing stdin on EOF |

## Bonuses (implemented in this project)

| Bonus | Where in code | Test |
|---|---|---|
| **Cookies + session management** | `/session` branch in `Connection::onReadable`, `HttpRequest::getCookieValue`, `HttpResponse::buildResponseWithCookie` | `02-evaluation.md` TC-07 |
| **Multiple CGI** (by different extensions) | `LocationConfig::cgiHandlers` — map `ext → interpreter`; `conf/tester.conf:27-28` (`.py`, `.sh`) | TC-06 |

> Bonuses count **only if mandatory is 100% done**. Check the mandatory part first.

---

Next: [`02-evaluation.md`](02-evaluation.md) — how to verify all of this by hand.
