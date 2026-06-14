# 01 — Требования сабжекта (mandatory + bonus)

Структурированный пересказ требований проекта `webserv` с привязкой **«требование → где в коде»**.
Колонка «Где в коде» — это точка, с которой начинать проверку на защите.

## Общие требования (mandatory)

| # | Требование | Где в коде / как проверить |
|---|---|---|
| 1 | Программа на **C++98**, компиляция `-Wall -Wextra -Werror` | `Makefile:27` (`-std=c++98`, + `-fsanitize=address -g3`) |
| 2 | Свой `Makefile` без relink (`all/clean/fclean/re`) | `Makefile:51-78` |
| 3 | Запуск: `./webserv [config]`; есть дефолтный конфиг | `src/main.cpp:30`, `ConfigLoader::loadDefault()` |
| 4 | Сервер **не должен падать** ни при каких обстоятельствах | `try/catch` в `main.cpp:32`, аккуратное закрытие fd |
| 5 | **Один** `poll()` (или эквивалент) на всё: и чтение, и запись | `src/Server.cpp:404` (единственный `::poll`) |
| 6 | `poll()` проверяет **read и write одновременно** | `pollfd.events` = `POLLIN\|POLLOUT` (`buildPollFds`, `wantedPollEvents`) |
| 7 | **Любой** `read`/`recv`/`write`/`send` — только после `poll`, и fd закрывается при ошибке I/O | `Connection::onReadable/onWritable`, без проверки `errno` после I/O (`Server.cpp:367`) |
| 8 | Неблокирующие fd (сокеты и пайпы CGI) | `setNonBlocking()` в `acceptPendingConnections` и `startCgi` |
| 9 | Точные **HTTP статус-коды** | `src/HttpResponse.cpp:26` (`reasonPhrase`) |
| 10 | **Default error pages**, если не заданы пользователем | `HttpResponse::buildErrorResponse` |
| 11 | Методы как минимум **GET, POST, DELETE** | `Connection::onReadable` (ветки), `handleDelete`, `handleUpload` |
| 12 | Статика: отдача файлов | `FilesystemHandler::buildFileSystemReply` |
| 13 | **Upload** файлов клиентом | `Connection::handleUpload` (`src/Connection.cpp:405`) |
| 14 | Один и тот же сервер слушает несколько портов | `ServerConfig::listens` (вектор), `setupListenSockets` |
| 15 | Работает с реальным браузером | проверяется вручную (см. `02-evaluation.md`) |
| 16 | Стрессоустойчивость (не виснет под нагрузкой) | event loop + лимиты `maxHeaderBytes/maxBodyBytes` |

## Конфигурационный файл (mandatory)

Формат — nginx-подобный. Реализованные директивы (см. парсер `src/ConfigParser.cpp` и структуры `include/Config.hpp`):

| Директива | Уровень | Структура-поле | Проверить в |
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

Ключевая идея конфигов — **наследование**: пара `hasX` + `X` отличает «не задано» от «задано пустое/false».
Если `location` не задал `root`, он наследует серверный (`buildEffectiveConfig`, см. [`04-config.md`](04-config.md)).

## CGI (mandatory)

| Требование | Где в коде |
|---|---|
| Запуск CGI по расширению | `Http::isCgiRequest` (`src/CgiHandler.cpp:264`) |
| Передача тела запроса в stdin, чтение stdout | `Connection::onCgiEvent` (`src/Connection.cpp:1077`) |
| Корректная работа с относительными путями (chdir в каталог скрипта) | `startCgi` → `::chdir(workDir)` (`src/Connection.cpp:1027`) |
| Переменные окружения CGI (метод, query, content-length…) | `prepareCgiArgs` (`src/CgiHandler.cpp:113-139`) |
| Сервер сам обрабатывает chunked/EOF корректно | парсинг `Transfer-Encoding: chunked` + закрытие stdin при EOF |

## Бонусы (реализованы в этом проекте)

| Бонус | Где в коде | Тест |
|---|---|---|
| **Cookies + session management** | ветка `/session` в `Connection::onReadable`, `HttpRequest::getCookieValue`, `HttpResponse::buildResponseWithCookie` | `02-evaluation.md` TC-07 |
| **Несколько CGI** (по разным расширениям) | `LocationConfig::cgiHandlers` — map `ext → interpreter`; `conf/tester.conf:27-28` (`.py`, `.sh`) | TC-06 |

> Бонусы оцениваются **только если mandatory выполнен на 100%**. Сначала проверяй обязательную часть.

---

Дальше: [`02-evaluation.md`](02-evaluation.md) — как всё это проверить руками.
