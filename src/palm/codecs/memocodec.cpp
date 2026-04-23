#include "memocodec.h"

#include <cstring>

#include "palmtext.h"

extern "C" {
#include <pi-buffer.h>
#include <pi-memo.h>
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

} // namespace

QByteArray encodeMemo(const Memo &memo)
{
    const QByteArray wire = encodePalmText(memo.text);

    Memo_t palm{};
    QByteArray mutableCopy = wire;
    mutableCopy.append('\0');
    palm.text = mutableCopy.data();

    ScopedBuffer out(mutableCopy.size() + 1);
    if (!out.buf) {
        return {};
    }
    if (pack_Memo(&palm, out.buf, memo_v1) < 0) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(out.buf->data),
                      static_cast<int>(out.buf->used));
}

std::optional<Memo> decodeMemo(QByteArrayView bytes)
{
    if (bytes.isEmpty()) {
        return Memo{};
    }

    ScopedBuffer in(bytes.size());
    if (!in.buf) {
        return std::nullopt;
    }
    std::memcpy(in.buf->data, bytes.data(), bytes.size());
    in.buf->used = bytes.size();

    Memo_t palm{};
    if (unpack_Memo(&palm, in.buf, memo_v1) < 0) {
        return std::nullopt;
    }

    Memo memo;
    memo.text = decodePalmText(palm.text);
    free_Memo(&palm);
    return memo;
}

} // namespace WildPalms::PalmCodecs
