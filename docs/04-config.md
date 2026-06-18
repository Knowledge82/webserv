# 04 — Configuration layer

## Purpose

Turn a text config (nginx-like) into a tree of C++ structs, and then, per request, into a **single** set of
final rules for a concrete URI. The pipeline:

```
.conf file → Tokenizer (tokens) → ConfigParser (grammar) → Config (tree of structs)
                                                                  │
                                      per request: selectLocation + buildEffectiveConfig
                                                                  ▼
                                                          EffectiveConfig (merged rules)
```

## Files and key functions

| What | Where |
|---|---|
| Load entry point | `ConfigLoader::loadFromFile` / `loadDefault` — `src/ConfigLoader.cpp:17` |
| Lexer | `Tokenizer::next` — `src/ConfigTokenizer.cpp`, tokens `T_WORD/T_LBRACE/T_RBRACE/T_SEMI/T_EOF` (`include/ConfigTokenizer.hpp:22`) |
| Parser | `ConfigParser::parseConfig/parseServer/parseLocation` — `src/ConfigParser.cpp` |
| Directive application | `applyServerDirective` (`:227`), `applyLocationDirective` (`:301`) |
| Structs | `ListenConfig/LocationConfig/ServerConfig/Config` — `include/Config.hpp` |
| Per-request merge | `selectLocation` (`src/Connection.cpp:59`), `buildEffectiveConfig` (`src/Connection.cpp:85`) |

## Diagram: loading and applying the config

```mermaid
flowchart LR
    F[conf/*.conf] --> T[Tokenizer.next<br/>T_WORD/LBRACE/RBRACE/SEMI]
    T --> P[ConfigParser<br/>parseServer / parseLocation]
    P --> A[apply*Directive<br/>fills hasX + X]
    A --> C[(Config:<br/>servers[].locations[])]
    C -. per request .-> SL[selectLocation<br/>longest prefix]
    SL --> BE[buildEffectiveConfig<br/>server → location override]
    BE --> E[(EffectiveConfig)]
```

## The "inheritance + distinguish unset / set" idea

In each struct a field is stored as a **pair** `hasX` + `X`. This distinguishes "directive not specified"
(must inherit from the parent) from "specified as empty/false". Example from `applyServerDirective`:

```cpp
// src/ConfigParser.cpp:248
if (name == "root")
{
    if (args.size() != 1)
        throw parseError(nameTok, "root expects 1 argument");
    srv.hasRoot = true;     // ← mark that root was EXPLICITLY set
    srv.root = args[0];
    return;
}
```

The server→location merge happens in `buildEffectiveConfig`: first take the server value, then, **if** the
location overrode it — overwrite:

```cpp
// src/Connection.cpp:85  (simplified)
EffectiveConfig eff;
if (srv.hasRoot)            { eff.hasRoot = true; eff.root = srv.root; }     // base — server
if (loc && loc->hasRoot)    { eff.hasRoot = true; eff.root = loc->root; }    // override — location
// similarly for index, autoindex, client_max_body_size, allowed_methods, cgi, redirect…
```

And the right `location` is chosen by the **longest matching prefix** of the URI:

```cpp
// src/Connection.cpp:59  selectLocation()
const LocationConfig *best = NULL;
std::size_t bestLen = 0;
for (std::size_t i = 0; i < locations.size(); ++i) {
    const std::string &prefix = locations[i].prefix;
    if (!Http::startsWithPrefix(uri, prefix))   // URI must start with the prefix
        continue;
    if (prefix.size() >= bestLen) {             // longer than previous → more specific
        best = &locations[i];
        bestLen = prefix.size();
    }
}
return best;
```

**Explanation.** `Config` stores "as written in the file" (static). `EffectiveConfig` is "how to apply it to a
given URI" (dynamic, computed per request). The default config (`loadDefault`) is just one `server{}` with one
default `listen`, no files on disk (`src/ConfigLoader.cpp:24`).

## What to look at during review / common bugs

- **One listen per server?** No — `ServerConfig::listens` is a vector, multiple ports are supported.
  Check `conf/2serv.conf` (see `02-evaluation.md` TC-12).
- **Does `--check-config` actually validate?** The parser throws `parseError` on unknown directives
  (`ConfigParser.cpp:298`) and a malformed `listen` — a config with an error must not be silently swallowed.
- **Does location-over-server override** work for all fields? Compare the list of assignments in
  `buildEffectiveConfig` against the fields of `EffectiveConfig` (`include/EffectiveConfig.hpp`).
- **Longest prefix**: with overlapping `location /` and `location /cgi-bin/`, the longer one must win. Tested by
  the fact that `/cgi-bin/test.py` goes to CGI, not static.
- **The tokenizer is simple**: no quotes or escapes (`include/ConfigTokenizer.hpp:57`) — paths with spaces are
  intentionally unsupported. That's fine for the subject, but worth knowing at the defense.

---

Next: [`05-server-eventloop.md`](05-server-eventloop.md) — what `Server` does with a ready config.
