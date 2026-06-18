# Гайд по ревью проекта `webserv` (42 Barcelona)

Это набор учебных материалов для **проверки, защиты и отладки** проекта `webserv` —
неблокирующего HTTP/1.1 сервера на C++98. Гайд написан так, чтобы студент-ревьюер мог:

1. понять, **что требует сабжект** (mandatory + bonus);
2. **проверить руками** каждую функциональность воспроизводимыми тест-кейсами (curl/nc);
3. **разобраться в коде** по модулям: схема → диаграмма потока → реальный сниппет → на что смотреть.

## Как пользоваться

- Идёшь на защиту — открываешь [`02-evaluation.md`](02-evaluation.md), поднимаешь сервер и прогоняешь чек-лист.
- Не понимаешь, как устроена конкретная часть — открываешь соответствующий модульный файл (04–10).
- Хочешь увидеть общую картину — начинаешь с [`03-architecture.md`](03-architecture.md).

## Оглавление

| Файл | О чём |
|---|---|
| [`01-requirements.md`](01-requirements.md) | Требования сабжекта (mandatory + bonus) и где они в коде |
| [`02-evaluation.md`](02-evaluation.md) | Чек-лист защиты + воспроизводимые тест-кейсы (TC-01…) |
| [`03-architecture.md`](03-architecture.md) | Структура репозитория, схема модулей, общий поток запроса |
| [`04-config.md`](04-config.md) | Конфиг: Loader → Tokenizer → Parser → Config → EffectiveConfig |
| [`05-server-eventloop.md`](05-server-eventloop.md) | `Server`: event loop на `poll()`, диспетчеризация fd |
| [`06-connection.md`](06-connection.md) | `Connection`: state machine соединения + роутинг |
| [`07-http-request.md`](07-http-request.md) | `HttpRequest`: инкрементальный парсер запроса |
| [`08-http-response.md`](08-http-response.md) | `HttpReply` (модель) + `HttpResponse` (сериализация) |
| [`09-static-files.md`](09-static-files.md) | `Path` / `Filesystem` / `FilesystemHandler` / `Autoindex` / `Mime` |
| [`10-cgi.md`](10-cgi.md) | CGI: `CgiHandler` + CGI-ветка `Connection` (fork/execve/pipe) |
| [`11-flow-walkthrough.md`](11-flow-walkthrough.md) | Сквозной flow по 4 этапам + лист защиты (Q&A ревьюера, siege) |
## Дополнительная теория (уже есть в репозитории)

Чтобы не дублировать общую теорию, для базовых понятий смотри существующие материалы:

- [`../README.md`](../README.md) — большой разбор: что такое веб-сервер, TCP/HTTP, I/O-мультиплексирование.
- [`../glossary.md`](../glossary.md) — глоссарий терминов (сокеты, fd, `poll`, `EAGAIN` и т.д.).

> Все ссылки на код в этом гайде даны в формате `src/Файл.cpp:NN` — номер строки кликабелен в GitHub/IDE.
> Если строки «уехали» после правок, ориентируйся по именам функций — они указаны рядом.
