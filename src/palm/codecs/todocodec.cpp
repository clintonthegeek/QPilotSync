#include "todocodec.h"

#include <cstring>
#include <ctime>

#include "palmtext.h"

extern "C" {
#include <pi-buffer.h>
#include <pi-todo.h>
}

namespace WildPalms::PalmCodecs {

namespace {

struct ScopedBuffer {
    pi_buffer_t *buf = nullptr;
    explicit ScopedBuffer(std::size_t initial = 256) { buf = pi_buffer_new(initial); }
    ~ScopedBuffer() { if (buf) pi_buffer_free(buf); }
    ScopedBuffer(const ScopedBuffer &) = delete;
    ScopedBuffer &operator=(const ScopedBuffer &) = delete;
};

struct ScopedToDo {
    ToDo_t t{};
    ~ScopedToDo() { free_ToDo(&t); }
    ScopedToDo() = default;
    ScopedToDo(const ScopedToDo &) = delete;
    ScopedToDo &operator=(const ScopedToDo &) = delete;
};

char *qstringToPalmDup(const QString &s)
{
    if (s.isEmpty()) {
        return nullptr;
    }
    const QByteArray wire = encodePalmText(s);
    char *out = static_cast<char *>(std::malloc(wire.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, wire.constData(), wire.size());
    out[wire.size()] = '\0';
    return out;
}

QString palmDupToQString(const char *s)
{
    return s ? decodePalmText(s) : QString();
}

} // namespace

QByteArray encodeTodo(const Todo &todo)
{
    ScopedToDo wrap;
    ToDo_t &t = wrap.t;
    t.indefinite = todo.hasIndefiniteDue ? 1 : 0;
    if (!todo.hasIndefiniteDue && todo.due.isValid()) {
        t.due.tm_year = todo.due.date().year() - 1900;
        t.due.tm_mon  = todo.due.date().month() - 1;
        t.due.tm_mday = todo.due.date().day();
    }
    t.priority   = todo.priority;
    t.complete   = todo.isComplete ? 1 : 0;
    t.description = qstringToPalmDup(todo.description);
    t.note        = qstringToPalmDup(todo.note);

    ScopedBuffer out(512);
    if (!out.buf) return {};
    if (pack_ToDo(&t, out.buf, todo_v1) < 0) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(out.buf->data),
                      static_cast<int>(out.buf->used));
}

std::optional<Todo> decodeTodo(QByteArrayView bytes)
{
    if (bytes.isEmpty()) return std::nullopt;
    ScopedBuffer in(bytes.size());
    if (!in.buf) return std::nullopt;
    std::memcpy(in.buf->data, bytes.data(), bytes.size());
    in.buf->used = bytes.size();

    ScopedToDo wrap;
    if (unpack_ToDo(&wrap.t, in.buf, todo_v1) < 0) {
        return std::nullopt;
    }
    const ToDo_t &t = wrap.t;

    Todo todo;
    todo.hasIndefiniteDue = t.indefinite != 0;
    if (!todo.hasIndefiniteDue) {
        QDate d(t.due.tm_year + 1900, t.due.tm_mon + 1, t.due.tm_mday);
        if (d.isValid()) {
            todo.due = QDateTime(d, QTime(0, 0));
        }
    }
    todo.priority    = t.priority;
    todo.isComplete  = t.complete != 0;
    todo.description = palmDupToQString(t.description);
    todo.note        = palmDupToQString(t.note);
    return todo;
}

} // namespace WildPalms::PalmCodecs
