# 10 — CGI: CgiHandler + CGI-ветка Connection

## Назначение

Запуск внешних скриптов (Python, Bash, …) по правилам CGI/1.1: подготовить окружение и аргументы,
форкнуть процесс, передать тело запроса в его `stdin`, прочитать `stdout`, разобрать его как HTTP-ответ.
Всё **неблокирующе** — пайпы CGI участвуют в том же `poll()`, что и сокеты.

Логика разнесена на два места:

- **`CgiHandler`** (`namespace Http`) — чистые функции без процессов: проверка «это CGI?», подготовка
  аргументов/окружения, парсинг вывода.
- **CGI-ветка `Connection`** — управление процессом и пайпами через state machine (`state_ == CGI`).

## Файлы и ключевые функции

| Что | Где |
|---|---|
| Это CGI-запрос? (по расширению) | `Http::isCgiRequest` — `src/CgiHandler.cpp:264` |
| Подготовка exe/script/workdir/env | `Http::prepareCgiArgs` — `src/CgiHandler.cpp:56` |
| Разбор вывода скрипта | `Http::parseCgiOutput` — `src/CgiHandler.cpp:184` |
| Запуск процесса (fork/execve/pipe) | `Connection::startCgi` — `src/Connection.cpp:943` |
| Неблокирующий обмен с процессом | `Connection::onCgiEvent` — `src/Connection.cpp:1077` |
| Какие события пайпов нужны poll | `wantedCgiStdinEvents` / `wantedCgiStdoutEvents` — `src/Connection.cpp:893/906` |
| Уборка (kill + close) | `closeAllFdsAndKillCgiIfAny` — `src/Connection.cpp:917` |

**Бонус «несколько CGI»**: интерпретатор выбирается динамически по расширению из `loc->cgiHandlers`
(map `ext → exe`), поэтому `.py` и `.sh` в одном `location` работают одновременно (`CgiHandler.cpp:71`).

## Диаграмма: жизненный цикл CGI-запроса

```mermaid
sequenceDiagram
    participant C as Connection (parent)
    participant P as poll loop
    participant K as CGI child
    C->>C: startCgi: pipe(in), pipe(out)
    C->>K: fork()
    K->>K: dup2(in→stdin, out→stdout), chdir(workDir), execve(interp, script)
    C->>C: close чужих концов, setNonBlocking, state_ = CGI
    loop пока пайпы открыты
        P->>C: onCgiEvent(stdin, POLLOUT)
        C->>K: write(body → stdin); закрыть stdin при EOF
        P->>C: onCgiEvent(stdout, POLLIN)
        K->>C: read(stdout) → cgiOut_
    end
    C->>C: оба пайпа закрыты → waitpid
    alt процесс упал (exit!=0 / сигнал) или таймаут 120с
        C->>C: makeErrorReply(500/504)
    else exit 0
        C->>C: parseCgiOutput(cgiOut_) → makeReply
    end
    C->>C: state_ = WRITING (ответ в out_)
```

## Сниппет: fork + execve (ядро `startCgi`)

```cpp
// src/Connection.cpp:943  (сокращённо)
int inPipe[2], outPipe[2];
::pipe(inPipe); ::pipe(outPipe);
setNonBlocking(inPipe[1]);   // концы СО СТОРОНЫ СЕРВЕРА — неблокирующие (для poll)
setNonBlocking(outPipe[0]);

pid_t pid = ::fork();
if (pid == 0) {                              // ── ДОЧЕРНИЙ ПРОЦЕСС ──
    ::close(inPipe[1]); ::close(outPipe[0]);
    ::dup2(inPipe[0],  STDIN_FILENO);        // тело запроса прилетит в stdin
    ::dup2(outPipe[1], STDOUT_FILENO);       // вывод скрипта уйдёт в pipe
    if (!workDir.empty()) ::chdir(workDir.c_str());  // относительные пути скрипта
    char **envp = buildEnvp(cgiEnv);
    char *argv[3] = { (char*)exeAbs.c_str(), (char*)scriptFile.c_str(), 0 };
    ::execve(argv[0], argv, envp);
    ::_exit(127);                            // execve не вернулся → ошибка запуска
}
// ── РОДИТЕЛЬ (веб-сервер) ──
::close(inPipe[0]); ::close(outPipe[1]);
cgiDeadline_ = std::time(0) + 120;           // таймаут от зависших скриптов
cgiStdinFd_  = inPipe[1];  cgiStdoutFd_ = outPipe[0];  cgiPid_ = pid;
cgiInData_   = req.getBody();                // тело отправим постепенно через poll
state_ = CGI;
if (cgiInData_.empty()) { ::close(cgiStdinFd_); cgiStdinFd_ = -1; cgiStdinClosed_ = true; } // GET: сразу EOF
```

**Объяснение.** Создаются два пайпа: `inPipe` (сервер → stdin скрипта) и `outPipe` (stdout скрипта → сервер).
После `fork` ребёнок перенаправляет свои `stdin/stdout` на нужные концы, делает `chdir` в каталог скрипта
(чтобы относительные пути внутри скрипта работали) и `execve` заменяет его образ на интерпретатор. Родитель
сохраняет **неблокирующие** концы в поля `Connection` и переходит в `state_ = CGI`. Тело запроса не пишется
сразу — оно отдаётся порциями в `onCgiEvent` по событию `POLLOUT`. Для запроса без тела (GET) stdin
закрывается немедленно, иначе скрипт будет вечно ждать ввод.

## Сниппет: финализация и разбор вывода (`onCgiEvent`)

```cpp
// src/Connection.cpp:1184  (когда оба пайпа закрыты)
if (cgiStdinClosed_ && cgiStdoutClosed_) {
    int st = 0; bool processFailed = false;
    if (cgiPid_ > 0) {
        ::waitpid(cgiPid_, &st, 0);                            // забираем зомби
        if (WIFEXITED(st) && WEXITSTATUS(st) != 0) processFailed = true;
        else if (WIFSIGNALED(st))                  processFailed = true;
        cgiPid_ = -1;
    }
    if (processFailed) { prepareReply(Http::makeErrorReply(500)); state_ = WRITING; return true; }

    int status = 200; std::string type = "text/plain", body;
    if (!Http::parseCgiOutput(status, type, body, cgiOut_))    // разбор "Status:/Content-Type:\n\n тело"
        prepareReply(Http::makeErrorReply(500));
    else
        prepareReply(Http::makeReply(status, type, body));     // → state_ = WRITING
    return true;
}
```

`parseCgiOutput` отделяет CGI-заголовки от тела по первому `\r\n\r\n` (или `\n\n`) и читает `Status:` /
`Content-Type:` (`src/CgiHandler.cpp:184`). Окружение для скрипта (`REQUEST_METHOD`, `QUERY_STRING`,
`CONTENT_LENGTH`, `SCRIPT_FILENAME`, `PATH_INFO`, …) формируется в `prepareCgiArgs`
(`src/CgiHandler.cpp:113-139`).

## На что смотреть на ревью / типичные баги

- **Неблокирующий CGI**: сервер не должен зависать на медленном/висящем скрипте. Проверь таймаут
  `cgiDeadline_` (120 с) → 504/500 (`src/Connection.cpp:1084`), и что параллельные запросы обслуживаются.
- **Stdin закрывается** после отправки тела (или сразу при пустом теле) — иначе скрипт ждёт EOF вечно
  (`src/Connection.cpp:1065` и `:1164`).
- **`waitpid` есть** — иначе накопятся зомби-процессы. Проверка `WIFEXITED/WIFSIGNALED` → 500 при падении.
- **Относительные пути**: `chdir(workDir)` перед `execve` (`:1027`); путь скрипта строится через
  `safeJoin/safeJoinAlias` (тот же traversal-щит, что и для статики).
- **Окружение CGI/1.1**: должны присутствовать `REQUEST_METHOD`, `QUERY_STRING`, `CONTENT_LENGTH`
  (для POST), `SCRIPT_FILENAME`. Сверь список в `prepareCgiArgs`.
- **Интерпретатор из конфига существует?** Если `cgi .py /opt/pyenv/shims/python3`, а такого пути нет —
  `execve` упадёт, ребёнок сделает `_exit(127)`, родитель вернёт 500. Это корректное поведение (сервер жив),
  но на защите легко принять за «баг CGI» — проверь окружение.
- **Мульти-CGI (bonus)**: `.py` и `.sh` в одном `location` → разные интерпретаторы из `cgiHandlers`
  (`conf/tester.conf:27-28`).

---

Назад к оглавлению: [`README.md`](README.md).
