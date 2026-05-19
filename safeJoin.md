## Итог этапа: `containsDotDot()` → `safeJoin()` (URL decode + normalize + защита от traversal)

На этом этапе мы убрали “наивную” защиту путей и заменили её на нормальную, системную схему обработки URI
перед маппингом в filesystem path.

---

### Что было раньше (проблема)

Раньше в `Connection::onReadable()` защита от path traversal выглядела так:

- строили путь строковым склеиванием:
  - `path = joinPath(root, uri.substr(1))`
- и перед этим проверяли:
  - `if (containsDotDot(uri)) -> 403`

Минусы такой схемы:

1) **Ловит не всё**
- `/..%2fsecret` или `/%2e%2e/secret` могло пройти (если нет URL decode).
- `%2e%2e` — это `..`, но `containsDotDot` этого не видит.

2) **False positives / нет нормализации**
- Путь `/a/b/../c` является нормальным и должен разрешаться (это “.. внутри root”),
  но `containsDotDot` бы его запретил.

3) **Нет единого “центра правды”**
- Логика безопасности размазана по месту использования, трудно расширять (autoindex, CGI, keep-alive).

---

### Что сделали (новый дизайн)

Ввели функцию:

- `safeJoin(root, rawUri, outFsPath, outStatus)`

и перенесли всю политику обработки URI в **одно место**.

Теперь `Connection` делает:
- проверка базовой формы URI (`/` в начале)
- `safeJoin()` → безопасный filesystem path
- дальше уже работает с диском (index/autoindex/read file)

---

### Политика `safeJoin` (зафиксировано)

`safeJoin()` реализует:

1) **Query игнорируется**
- URI может быть: `/img/logo.png?x=y`
- Для filesystem mapping берём только path часть: `/img/logo.png`

2) **Fragment `#...` запрещён**
- Если в request line внезапно есть `#`, считаем это невалидным → `400 Bad Request`
- Нормальный браузер fragment на сервер не отправляет, поэтому строгая политика ок.

3) **Percent-decoding `%XX`**
- Поддерживаем URL decode в path.
- Любая невалидная последовательность `%` → `400`.

4) **Запрещаем encoded slash `%2F/%2f`**
- После decode получился символ `'/'` из `%2F` → `400 Bad Request`
- Это снижает риск обходов через “закодированные слэши”.

5) **Нормализация сегментов**
- Разбиваем путь по `/` на сегменты.
- `.` игнорируем.
- `..` откатывает один сегмент.

6) **Запрет выхода выше root**
- Если встречается `..`, когда откатывать уже нечего → это попытка escape → `403 Forbidden`.

---

### Что конкретно поменяли в `Connection::onReadable()`

В блоке “MAPPING URI -> FILESYSTEM PATH”:

- полностью убрали:
  - `containsDotDot(uri)`
- заменили:
  - `path = joinPath(eff.root, uri.substr(1))`
  на:
  - `safeJoin(eff.root, uri, path, safeStatus)` + ответ `400/403` при ошибке

При этом:
- redirect `/dir` → `/dir/` по-прежнему строим из **URI**, а не из filesystem path
- autoindex использует **URI** для генерации ссылок и **fs path** для чтения директории

---

### Примеры поведения (ожидаемо)

| Запрос | Результат | Почему |
|---|---:|---|
| `GET /a/b.txt` | 200 | обычный путь |
| `GET /a/x/../b.txt` | 200 | нормализация `..` внутри root |
| `GET /../secret` | 403 | попытка уйти выше root |
| `GET /a%2Fb.txt` | 400 | `%2F` запрещён |
| `GET /a#b` | 400 | fragment запрещён |
| `GET /img/logo.png?x=y` | 200 | query отрезается |

---

### Быстрые тесты (curl / nc)

```bash
# OK: normal file
curl -v "http://127.0.0.1:8080/a/b.txt"

# OK: normalize .. inside root
curl -v "http://127.0.0.1:8080/a/x/../b.txt"

# 403: escape root
curl -v --path-as-is "http://127.0.0.1:8080/../secret"

# 400: encoded slash forbidden
curl -v "http://127.0.0.1:8080/a%2Fb.txt"

# 400: fragment forbidden (nc, because normal clients won't send fragments)
printf 'GET /a#b HTTP/1.1\r\nHost: x\r\n\r\n' | nc 127.0.0.1 8080
```

---

### Заметки / что дальше

- Сейчас `safeJoin` даёт “правильный безопасный fs path”.
- Следующий шаг: перестать трактовать `readFileToString()==false` как “404 всегда” и начать
  различать `403/404/500` через `stat()` (до чтения файла), сохраняя совместимость с subject.
