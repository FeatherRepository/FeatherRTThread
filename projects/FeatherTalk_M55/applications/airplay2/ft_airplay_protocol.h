#ifndef FT_AIRPLAY_PROTOCOL_H
#define FT_AIRPLAY_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define FT_AP_HEADER_MAX 4096U
#define FT_AP_BODY_MAX 16384U
enum { FT_AP_OK = 0, FT_AP_MORE = 1, FT_AP_INVALID = -1, FT_AP_LIMIT = -2,
       FT_AP_NOT_FOUND = -3 };
typedef struct {
    char method[32], path[256], content_type[64];
    uint32_t cseq;
    int http;
    const uint8_t *body;
    size_t body_bytes, consumed;
} ft_ap_request_t;
/* No NUL terminator required. Body borrows input; consumed excludes pipelined data. */
int ft_ap_request_parse(const uint8_t *data, size_t bytes, ft_ap_request_t *out);
/* HAP control record: LE16 payload length, 1..1024 bytes, then 16-byte AEAD tag.
 * Framing only: success NEVER means authentication succeeded. */
int ft_ap_record_size(const uint8_t *data, size_t bytes, size_t *record_bytes);
/* Validate the complete TLV envelope before exposing any pairing field. */
int ft_ap_tlv_get(const uint8_t *data, size_t bytes, uint8_t type,
                  uint8_t *out, size_t capacity, size_t *written);
int ft_ap_tlv_put(uint8_t *out, size_t capacity, uint8_t type,
                  const uint8_t *value, size_t bytes, size_t *written);
#endif
