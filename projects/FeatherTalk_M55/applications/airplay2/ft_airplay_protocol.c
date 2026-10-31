/* Bounded wire-format layer. No ESP-IDF, crypto, audio or allocation dependency. */
#include "ft_airplay_protocol.h"
#include <string.h>
#include <limits.h>
static size_t crlf(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i + 1 < n; ++i) if (p[i] == '\r' && p[i+1] == '\n') return i;
    return n;
}
static int equal(const uint8_t *p, size_t n, const char *s)
{
    size_t i;
    if (strlen(s) != n) return 0;
    for (i = 0; i < n; ++i) {
        unsigned c = p[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)s[i]) return 0;
    }
    return 1;
}
static int decimal(const uint8_t *p, size_t n, uint32_t *v)
{
    size_t i;
    uint32_t value = 0;
    if (!n) return FT_AP_INVALID;
    for (i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9') return FT_AP_INVALID;
        if (value > (UINT32_MAX - (p[i] - '0')) / 10U) return FT_AP_LIMIT;
        value = value * 10U + p[i] - '0';
    }
    *v = value;
    return FT_AP_OK;
}
int ft_ap_request_parse(const uint8_t *data, size_t bytes, ft_ap_request_t *out)
{
    size_t end, line, pos, a, b, i;
    uint32_t length = 0;
    int have_length = 0, have_cseq = 0;
    if (!out || (!data && bytes)) return FT_AP_INVALID;
    memset(out, 0, sizeof(*out));
    for (end = 0; end + 3 < bytes && end + 4 <= FT_AP_HEADER_MAX; ++end)
        if (!memcmp(data + end, "\r\n\r\n", 4)) break;
    if (end + 3 >= bytes || end + 4 > FT_AP_HEADER_MAX)
        return bytes >= FT_AP_HEADER_MAX ? FT_AP_LIMIT : FT_AP_MORE;
    end += 4;
    for (i = 0; i < end; ++i)
        if (data[i] == 0 || data[i] > 126 || (data[i] < 32 &&
            data[i] != '\r' && data[i] != '\n' && data[i] != '\t')) return FT_AP_INVALID;
    line = crlf(data, end);
    for (a = 0; a < line && data[a] != ' '; ++a) {}
    if (!a || a >= sizeof(out->method) || a == line) return FT_AP_INVALID;
    for (b = a + 1; b < line && data[b] != ' '; ++b) {}
    if (b == a + 1 || b == line || b - a - 1 >= sizeof(out->path)) return FT_AP_INVALID;
    if (equal(data+b+1, line-b-1, "http/1.1")) out->http = 1;
    else if (!equal(data+b+1, line-b-1, "rtsp/1.0")) return FT_AP_INVALID;
    memcpy(out->method, data, a);
    memcpy(out->path, data+a+1, b-a-1);
    pos = line + 2;
    while (pos < end - 2) {
        const uint8_t *p = data + pos;
        size_t n = crlf(p, end-pos), colon, first, last;
        for (i = 0; i < n; ++i) if (p[i] == '\r' || p[i] == '\n') return FT_AP_INVALID;
        for (colon = 0; colon < n && p[colon] != ':'; ++colon) {}
        if (!colon || colon == n) return FT_AP_INVALID;
        for (i = 0; i < colon; ++i)
            if (!((p[i] >= 'A' && p[i] <= 'Z') || (p[i] >= 'a' && p[i] <= 'z') ||
                  (p[i] >= '0' && p[i] <= '9') || p[i] == '-')) return FT_AP_INVALID;
        first = colon + 1; last = n;
        while (first < last && (p[first] == ' ' || p[first] == '\t')) ++first;
        while (last > first && (p[last-1] == ' ' || p[last-1] == '\t')) --last;
        if (equal(p, colon, "content-length")) {
            int r;
            if (have_length++) return FT_AP_INVALID;
            r = decimal(p+first, last-first, &length);
            if (r) return r;
            if (length > FT_AP_BODY_MAX) return FT_AP_LIMIT;
        } else if (equal(p, colon, "cseq")) {
            int r;
            if (have_cseq++) return FT_AP_INVALID;
            r = decimal(p+first, last-first, &out->cseq);
            if (r) return r;
            if (out->cseq > INT_MAX) return FT_AP_LIMIT;
        } else if (equal(p, colon, "content-type")) {
            if (last-first >= sizeof(out->content_type) || out->content_type[0]) return FT_AP_INVALID;
            memcpy(out->content_type,p+first,last-first);
        } else if (equal(p, colon, "transfer-encoding")) return FT_AP_INVALID;
        pos += n + 2;
    }
    if (!out->http && !have_cseq) return FT_AP_INVALID;
    if (bytes - end < length) return FT_AP_MORE;
    out->body = data + end; out->body_bytes = length; out->consumed = end + length;
    return FT_AP_OK;
}
int ft_ap_record_size(const uint8_t *data, size_t bytes, size_t *record_bytes)
{
    size_t n;
    if (!record_bytes || (!data && bytes)) return FT_AP_INVALID;
    *record_bytes = 0;
    if (bytes < 2) return FT_AP_MORE;
    n = data[0] | ((size_t)data[1] << 8);
    if (!n || n > 1024) return FT_AP_INVALID;
    *record_bytes = n + 18;
    return bytes < *record_bytes ? FT_AP_MORE : FT_AP_OK;
}
int ft_ap_tlv_get(const uint8_t *data, size_t bytes, uint8_t type,
                  uint8_t *out, size_t capacity, size_t *written)
{
    size_t pos = 0, total = 0, previous = 0;
    int state = 0;
    if (!written || (!data && bytes) || (!out && capacity)) return FT_AP_INVALID;
    *written = 0;
    /* First pass is transactional: malformed tails never expose a prefix. */
    while (pos < bytes) {
        size_t n;
        if (bytes - pos < 2) return FT_AP_INVALID;
        n = data[pos+1];
        if (n > bytes-pos-2) return FT_AP_INVALID;
        if (data[pos] == type) {
            if (state == 2 || (state == 1 && previous != 255)) return FT_AP_INVALID;
            state = 1; previous = n;
            if (n > capacity-total) return FT_AP_LIMIT;
            total += n;
        } else if (state == 1) state = 2;
        pos += n + 2;
    }
    if (!state) return FT_AP_NOT_FOUND;
    for (pos = 0, total = 0; pos < bytes; pos += (size_t)data[pos+1] + 2) {
        size_t n = data[pos+1];
        if (data[pos] == type && n) { memcpy(out+total, data+pos+2, n); total += n; }
    }
    *written = total;
    return FT_AP_OK;
}
int ft_ap_tlv_put(uint8_t *out, size_t capacity, uint8_t type,
                  const uint8_t *value, size_t bytes, size_t *written)
{
    size_t chunks, pos = 0, used = 0;
    if (!written || (!out && capacity) || (!value && bytes)) return FT_AP_INVALID;
    *written = 0;
    chunks = bytes ? bytes/255 + (bytes%255 != 0) : 1;
    if (bytes > capacity || chunks > (capacity-bytes)/2) return FT_AP_LIMIT;
    do {
        size_t n = bytes-pos > 255 ? 255 : bytes-pos;
        out[used++] = type; out[used++] = (uint8_t)n;
        if (n) memcpy(out+used, value+pos, n);
        used += n; pos += n;
    } while (pos < bytes);
    *written = used;
    return FT_AP_OK;
}
