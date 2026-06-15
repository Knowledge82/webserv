# 04 — Конфигурационный слой

## Назначение

Превратить текстовый конфиг (nginx-подобный) в дерево C++-структур, а затем при каждом запросе — в **один**
набор итоговых правил для конкретного URI. Конвейер:

```
файл .conf → Tokenizer (лексемы) → ConfigParser (грамматика) → Config (дерево структур)
                                                                      │
                                          на каждый запрос: selectLocation + buildEffectiveConfig
                                                                      ▼
                                                              EffectiveConfig (слитые правила)
```

## Файлы и ключевые функции

| Что | Где |
|---|---|
| Точка входа загрузки | `ConfigLoader::loadFromFile` / `loadDefault` — `src/ConfigLoader.cpp:17` |
| Лексер | `Tokenizer::next` — `src/ConfigTokenizer.cpp`, токены `T_WORD/T_LBRACE/T_RBRACE/T_SEMI/T_EOF` (`include/ConfigTokenizer.hpp:22`) |
| Парсер | `ConfigParser::parseConfig/parseServer/parseLocation` — `src/ConfigParser.cpp` |
| Применение директив | `applyServerDirective` (`:227`), `applyLocationDirective` (`:301`) |
| Структуры | `ListenConfig/LocationConfig/ServerConfig/Config` — `include/Config.hpp` |
| Слияние под запрос | `selectLocation` (`src/Connection.cpp:59`), `buildEffectiveConfig` (`src/Connection.cpp:85`) |

## Диаграмма: загрузка и применение конфига

```mermaid
flowchart LR
    F[conf/*.conf] --> T[Tokenizer.next<br/>T_WORD/LBRACE/RBRACE/SEMI]
    T --> P[ConfigParser<br/>parseServer / parseLocation]
    P --> A[apply*Directive<br/>заполняет hasX + X]
    A --> C[(Config:<br/>servers[].locations[])]
    C -. на каждый запрос .-> SL[selectLocation<br/>самый длинный префикс]
    SL --> BE[buildEffectiveConfig<br/>server → location override]
    BE --> E[(EffectiveConfig)]
```

## Идея «наследование + отличие не-задано / задано»

В каждой структуре поле хранится **парой** `hasX` + `X`. Это позволяет отличить «директива не указана»
(нужно наследовать у родителя) от «указана пустая/false». Пример из `applyServerDirective`:

```cpp
// src/ConfigParser.cpp:248
if (name == "root")
{
    if (args.size() != 1)
        throw parseError(nameTok, "root expects 1 argument");
    srv.hasRoot = true;     // ← отмечаем, что root ЗАДАН явно
    srv.root = args[0];
    return;
}
```

А слияние server→location происходит в `buildEffectiveConfig`: сначала берём серверное значение,
затем, **если** location переопределил — перетираем:

```cpp
// src/Connection.cpp:85  (упрощённо)
EffectiveConfig eff;
if (srv.hasRoot)            { eff.hasRoot = true; eff.root = srv.root; }     // база — server
if (loc && loc->hasRoot)    { eff.hasRoot = true; eff.root = loc->root; }    // override — location
// аналогично index, autoindex, client_max_body_size, allowed_methods, cgi, redirect…
```

А выбор нужного `location` — по **самому длинному совпавшему префиксу** URI:

```cpp
// src/Connection.cpp:59  selectLocation()
const LocationConfig *best = NULL;
std::size_t bestLen = 0;
for (std::size_t i = 0; i < locations.size(); ++i) {
    const std::string &prefix = locations[i].prefix;
    if (!Http::startsWithPrefix(uri, prefix))   // URI должен начинаться с префикса
        continue;
    if (prefix.size() >= bestLen) {             // длиннее предыдущего → точнее
        best = &locations[i];
        bestLen = prefix.size();
    }
}
return best;
```

**Объяснение.** `Config` хранит «как написано в файле» (статика). `EffectiveConfig` — это «как применить
к данному URI» (динамика, считается на каждый запрос). Дефолтный конфиг (`loadDefault`) — это просто один
`server{}` с одним `listen` по умолчанию, без файлов на диске (`src/ConfigLoader.cpp:24`).

## На что смотреть на ревью / типичные баги

- **Один listen на сервер?** Нет — `ServerConfig::listens` это вектор, поддерживается несколько портов.
  Проверь `conf/2serv.conf` (см. `02-evaluation.md` TC-12).
- **`--check-config` действительно валидирует?** Парсер бросает `parseError` на неизвестных директивах
  (`ConfigParser.cpp:298`) и кривом `listen` — конфиг с ошибкой не должен «молча» проглатываться.
- **Override location над server** работает для всех полей? Сверь список присваиваний в `buildEffectiveConfig`
  с полями `EffectiveConfig` (`include/EffectiveConfig.hpp`).
- **Самый длинный префикс**: при пересекающихся `location /` и `location /cgi-bin/` должен выигрывать более
  длинный. Тестируется тем, что `/cgi-bin/test.py` идёт в CGI, а не в статику.
- **Токенайзер прост**: нет кавычек и escape (`include/ConfigTokenizer.hpp:57`) — пути с пробелами не поддержаны
  сознательно. Это нормально для сабжекта, но стоит знать на защите.

---

Дальше: [`05-server-eventloop.md`](05-server-eventloop.md) — что делает `Server` с готовым конфигом.
