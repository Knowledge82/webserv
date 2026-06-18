# Соответствие кода обязательным требованиям (mandatory)

В данном разделе подробно описано соответствие кодовой базы сервера обязательным
требованиям технического задания (subject). Для каждого требования приведены:
**цитата из сабжекта** → **зачем это нужно** → **как это реализовано** → **сниппет кода** →
**пример проверки** (конфиг + `curl`).

---

## 1. Конфигурационный файл

> «Your program must use a configuration file, provided as an argument on the command line,
> or available in a default path.»

**Зачем это нужно.** Сервер не должен иметь «зашитых» (hardcoded) параметров. Он обязан
гибко настраиваться под разные сайты, порты и директории через внешний файл — как `nginx`.

**Как это сделано.** В `src/main.cpp` по количеству аргументов выбирается источник конфигурации:
путь к файлу → `ConfigLoader::loadFromFile`, иначе → `ConfigLoader::loadDefault` (порт 8080 в
памяти). Лексику разбирают `ConfigTokenizer` (токены) и `ConfigParser` (объекты + валидация).

**Сниппет** (`src/main.cpp:34-57`):

```cpp
Config cfg;
if (argc == 1)
    cfg = ConfigLoader::loadDefault();                 // дефолт в памяти (порт 8080)
else if (argc == 2)
{
    if (std::string(argv[1]) == "--check-config")      // только проверить и выйти
    {
        cfg = ConfigLoader::loadDefault();
        std::cout << GREEN << "OK: " << RESET << "default config" << std::endl;
        return 0;
    }
    cfg = ConfigLoader::loadFromFile(argv[1]);          // ./webserv conf/tester.conf
}
else if (argc == 3)                                    // ./webserv --check-config <file>
{
    if (std::string(argv[1]) != "--check-config") return printUsage();
    cfg = ConfigLoader::loadFromFile(argv[2]);
    std::cout << GREEN << "OK: " << RESET << argv[2] << std::endl;
    return 0;
}
Server s(cfg);
s.run();
```

**Пример проверки.**

```bash
./webserv --check-config conf/tester.conf   # синтаксис ОК → "OK: conf/tester.conf", exit 0
./webserv conf/tester.conf                  # запуск сервера на порту из конфига
```

---

## 2. Несколько `interface:port` (несколько сайтов)

> «Define all the interface:port pairs on which your server will listen to (defining multiple
> websites served by your program).»

**Зачем это нужно.** Один процесс должен обслуживать несколько сайтов на разных портах/адресах
одновременно — как виртуальные хосты в nginx.

**Как это сделано.** Каждый блок `server { ... }` → `ServerConfig`, каждая директива `listen` →
`ListenConfig` (`host:port`). В `Server::run()` для **каждой** пары открывается отдельный
слушающий сокет (`FD_LISTEN`), и все они обслуживаются **единым** `poll()`.

**Пример конфига** (`conf/2serv.conf` — два сайта):

```nginx
server {
    listen 127.0.0.1:8080;
    root ./www;
}
server {
    listen 127.0.0.1:8081;
    root ./www2;
}
```

**Пример проверки.**

```bash
curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8080/   # 200 — первый сайт
curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8081/   # 200 — второй сайт
```

---

## 3. Кастомные страницы ошибок

> «Set up default error pages.»

**Зачем это нужно.** Вместо «голого» кода ошибки отдавать осмысленную страницу.

**Как это сделано.** Любая ошибка собирается через `HttpResponse::buildErrorResponse(status)`.
Если пользовательская страница не задана — формируется встроенное тело `errorBody()`, а текст
статуса берётся из `reasonPhrase` (`src/HttpResponse.cpp:26`).

**Сниппет** (`src/HttpResponse.cpp`, как код → текстовая фраза):

```cpp
static std::string reasonPhrase(int code)
{
    switch (code)
    {
        case 200: return "OK";
        case 301: return "Moved Permanently";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default:  return "Error";
    }
}
```

**Пример проверки.**

```bash
curl -s -i "$BASE/notfound" | head -1     # HTTP/1.1 404 Not Found + непустое тело
```

---

## 4. Ограничение размера тела запроса

> «Set the maximum allowed size for client request bodies.»

**Зачем это нужно.** Защита от исчерпания памяти: клиент не должен «положить» сервер гигантским
телом запроса.

**Как это сделано.** Директива `client_max_body_size` попадает в `EffectiveConfig`. В
`Connection::onReadable` размер тела сверяется с лимитом → **413 Payload Too Large**.

**Сниппет** (`src/Connection.cpp:604-609`):

```cpp
if (eff.hasClientMaxBodySize && request_.getContentLength() > eff.clientMaxBodySize)
{
    out_ = HttpResponse::buildErrorResponse(413);   // Payload Too Large
    state_ = WRITING;
    return true;
}
```

**Пример конфига** (`conf/tester.conf:11-14` — лимит 100 байт на маршруте):
