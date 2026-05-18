## Итог: реализовали `autoindex` (directory listing) + redirect `/dir` → `/dir/`

### Что добавили (новый функционал)

1) **autoindex on/off (nginx-like)**
- Поддерживаем флаг `autoindex` как настройку, которая может быть:
  - задана на уровне `server` (дефолт для всех URI в этом server-block),
  - переопределена на уровне `location`.
- Это реализовано через **наследование (merge)**:
  - server = defaults
  - location = overrides
  - `EffectiveConfig` = итоговые настройки для конкретного URI.

2) **Редирект для директорий без завершающего `/`**
- Если клиент запросил URI вида `/docs` и это реально директория на диске,
  сервер отдаёт:
  - `301 Moved Permanently`
  - `Location: /docs/`
- Это нужно, чтобы относительные ссылки в autoindex/HTML работали корректно.

3) **HTML directory listing**
- Если запрошенный путь — директория, и:
  - index не задан, или index-файл отсутствует/не читается,
  - и effective `autoindex on`,
  то сервер генерирует HTML-страницу со списком файлов/директорий.
- Из листинга исключаются `.` и `..` (и мы **не добавляем ссылку на parent**).
- Для поддиректорий добавляем `/` в отображении и в ссылке.

---

### Как это работает внутри `Connection::onReadable()` (сценарий)

Пусть запрос: `GET /docs` или `GET /docs/`.

1) `recv()` → байты попадают в `in_`
2) `HttpRequest::parse(in_)` → `COMPLETE`
3) `selectLocation(server.locations, uri)` выбирает лучший `location` по **longest prefix match**
4) `buildEffectiveConfig(server, loc)` собирает effective настройки (server defaults + overrides)
5) если `return` задан → делаем redirect и выходим
6) если `allow_methods` запрещает метод → `405`
7) если метод не `GET` → `405`
8) проверяем `root`, guard `..`
9) строим filesystem path: `path = root + uri`
10) если `path` — директория:
    - если URI не оканчивается на `/` → `301` на `uri + "/"`, выходим
    - иначе пробуем `index` внутри директории (если задан):
      - если `index` найден → отдаём файл
    - иначе если `autoindex on` → генерим HTML listing и отдаём как `text/html`
    - иначе → `403 Forbidden`
11) если `path` — файл:
    - читаем `readFileToString(path)`
    - собираем `200 OK` + `Content-Type` по расширению
12) `state_ = WRITING`, дальше `onWritable()` отправляет `out_` кусками через `send()`

---

### Какие файлы/модули тронули

- `Connection.cpp`
  - добавили генерацию листинга через `opendir/readdir/closedir` (`<dirent.h>`)
  - добавили `endsWithSlash()`
  - изменили обработку директорий: redirect → index → autoindex → 403
  - сделали merge `autoindex` из server-level + location override

- `Config.hpp`
  - `ServerConfig` уже содержит `hasAutoindex/autoindex`
  - `LocationConfig` уже содержит `hasAutoindex/autoindex`

- `Config.cpp`
  - конструкторы `ServerConfig`/`LocationConfig` инициализируют `hasAutoindex=false`, `autoindex=false`

> Важно: чтобы server-level `autoindex on|off;` реально работал, `ConfigParser` должен парсить эту директиву в `server { ... }`.

---

### Тесты (curl)

Пример конфига:
```nginx
server {
  listen 127.0.0.1:8080;
  root ./www;
  index index.html;
  autoindex off;

  location /docs/ {
    autoindex on;
  }
}
```

Структура:
```
www/docs/a.txt
www/docs/images/
www/docs/images/logo.png
```

Команды:

1) Redirect `/docs` → `/docs/`:
```bash
curl -v http://127.0.0.1:8080/docs
```

2) Directory listing:
```bash
curl -v http://127.0.0.1:8080/docs/
```

3) Переход по ссылке из листинга (файл):
```bash
curl -v http://127.0.0.1:8080/docs/a.txt
```

4) Переход по ссылке из листинга (директория):
```bash
curl -v http://127.0.0.1:8080/docs/images
# ожидаем 301 на /docs/images/
curl -v http://127.0.0.1:8080/docs/images/
```

---

### Известные упрощения/долги (что будем улучшать дальше)

- Защита путей пока грубая (`containsDotDot`) — позже сделаем URL-decode + normalize + проверку “внутри root”.
- Ошибки чтения файлов пока сводятся к 404 (позже различим 403/404/500 по `errno`).
- Пока всегда `Connection: close` (keep-alive позже).
- Listing не сортирует файлы (можно добавить позже).
