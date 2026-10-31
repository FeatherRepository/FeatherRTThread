#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ft_airplay_protocol.h"
#define PARSE(s) ft_ap_request_parse((const uint8_t *)(s), sizeof(s)-1, &r)
int main(void)
{
    ft_ap_request_t r;
    const char request[] = "POST /pair-setup RTSP/1.0\r\ncSeQ: 23\r\ncontent-length: 3\r\n\r\n\6\1\1";
    uint8_t data[6000], out[1200], value[1024];
    size_t i, n, got, record;
    for (i = 0; i < sizeof(request)-1; ++i)
        assert(ft_ap_request_parse((const uint8_t *)request, i, &r) == FT_AP_MORE);
    memcpy(data, request, sizeof(request)-1);
    assert(ft_ap_request_parse(data, sizeof(request)-1, &r) == FT_AP_OK);
    assert(r.cseq == 23 && r.body_bytes == 3 && r.body[0] == 6);
    memcpy(data+sizeof(request)-1, request, sizeof(request)-1);
    assert(ft_ap_request_parse(data, 2*(sizeof(request)-1), &r) == FT_AP_OK);
    assert(r.consumed == sizeof(request)-1);
    assert(ft_ap_request_parse(data+r.consumed, sizeof(request)-1, &r) == FT_AP_OK);
    assert(PARSE("GET /info HTTP/1.1\r\n\r\n") == FT_AP_OK);
    assert(PARSE("OPTIONS * RTSP/1.0\r\nCSeq: 0\r\n\r\n") == FT_AP_OK);
    assert(PARSE("POST /x RTSP/1.0\r\nCSeq: 1\r\nContent-Length: -1\r\n\r\n") == FT_AP_INVALID);
    assert(PARSE("POST /x RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 4294967296\r\n\r\n") == FT_AP_LIMIT);
    assert(PARSE("POST /x RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 16385\r\n\r\n") == FT_AP_LIMIT);
    assert(PARSE("POST /x RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n") == FT_AP_INVALID);
    assert(PARSE("OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nCSeq: 2\r\n\r\n") == FT_AP_INVALID);
    assert(PARSE("OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nTransfer-Encoding: chunked\r\n\r\n") == FT_AP_INVALID);
    assert(PARSE("OPTIONS * RTSP/1.0\r\nCSeq: 1\0\r\n\r\n") == FT_AP_INVALID);
    memset(data, 'A', FT_AP_HEADER_MAX);
    assert(ft_ap_request_parse(data, FT_AP_HEADER_MAX, &r) == FT_AP_LIMIT);
    for (i = 0; i < sizeof(value); ++i) value[i] = (uint8_t)i;
    assert(ft_ap_tlv_put(data, sizeof(data), 3, value, sizeof(value), &n) == FT_AP_OK);
    assert(n == 1034);
    assert(ft_ap_tlv_get(data, n, 3, out, sizeof(out), &got) == FT_AP_OK);
    assert(got == sizeof(value) && !memcmp(value, out, got));
    memset(out, 0xA5, sizeof(out));
    assert(ft_ap_tlv_get(data, n-1, 3, out, sizeof(out), &got) == FT_AP_INVALID);
    assert(!got && out[0] == 0xA5);
    assert(ft_ap_tlv_get(data, n, 3, out, 10, &got) == FT_AP_LIMIT);
    assert(!got && out[0] == 0xA5);
    assert(ft_ap_tlv_put(out, 3, 3, value, 5, &got) == FT_AP_LIMIT);
    assert(!got && out[0] == 0xA5);
    { uint8_t bad[] = {6,1,1,6,1,2};
      assert(ft_ap_tlv_get(bad, sizeof(bad), 6, out, sizeof(out), &got) == FT_AP_INVALID); }
    { uint8_t bad[] = {6,1,1,7};
      assert(ft_ap_tlv_get(bad, sizeof(bad), 6, out, sizeof(out), &got) == FT_AP_INVALID); }
    assert(ft_ap_tlv_put(data, sizeof(data), 1, NULL, 0, &n) == FT_AP_OK);
    assert(n == 2 && ft_ap_tlv_get(data, n, 1, NULL, 0, &got) == FT_AP_OK && got == 0);
    assert(ft_ap_tlv_get(data, n, 2, out, sizeof(out), &got) == FT_AP_NOT_FOUND);
    data[0] = 0; data[1] = 4;
    assert(ft_ap_record_size(data, 1, &record) == FT_AP_MORE);
    assert(ft_ap_record_size(data, 1041, &record) == FT_AP_MORE && record == 1042);
    assert(ft_ap_record_size(data, 1042, &record) == FT_AP_OK);
    data[0] = 1;
    assert(ft_ap_record_size(data, 1042, &record) == FT_AP_INVALID);
    data[0] = data[1] = 0;
    assert(ft_ap_record_size(data, 20, &record) == FT_AP_INVALID);
    /* Deterministic malformed-packet smoke fuzzing, plus bounds invariants. */
    {
        uint32_t seed = 7;
        for (i = 0; i < 20000; ++i) {
            size_t j, len = i % sizeof(data);
            for (j = 0; j < len; ++j) { seed = seed*1664525U+1013904223U; data[j] = seed >> 24; }
            if (ft_ap_request_parse(data, len, &r) == FT_AP_OK) {
                assert(r.consumed <= len && r.body_bytes <= FT_AP_BODY_MAX);
                assert(r.body >= data && r.body+r.body_bytes <= data+len);
            }
            ft_ap_tlv_get(data, len, 3, out, sizeof(out), &got);
            assert(got <= sizeof(out));
        }
    }
    puts("PASS: RTSP boundaries/pipeline, HAP record framing, transactional TLV8, 20000 malformed inputs");
    return 0;
}
