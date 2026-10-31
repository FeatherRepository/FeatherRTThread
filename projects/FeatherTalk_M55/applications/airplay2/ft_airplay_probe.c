/* Development transport only. No mDNS advertisement, pairing or audio claims. */
#include <rtthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include "../feathertalk_wifi.h"
#include "../feathertalk_audio.h"
#include "ft_airplay_protocol.h"
#define AP_PROBE_PORT 7000
static volatile int s_active, s_stop, s_error;
static volatile unsigned s_requests, s_rejected;
static int probe_send(int fd, const char *data, size_t n)
{
    while (n && !s_stop) {
        int sent = send(fd, data, n, 0);
        if (sent <= 0) return -1;
        data += sent; n -= (size_t)sent;
    }
    return n ? -1 : 0;
}
static void probe_client(int fd, uint8_t *buffer)
{
    size_t used = 0;
    unsigned requests = 0;
    rt_tick_t started = rt_tick_get();
    struct timeval timeout = {1, 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) return;
    while (!s_stop && requests < 8 &&
           (rt_tick_t)(rt_tick_get()-started) < rt_tick_from_millisecond(5000)) {
        ft_ap_request_t req;
        int result = ft_ap_request_parse(buffer, used, &req);
        if (result == FT_AP_MORE) {
            int got;
            if (used == FT_AP_HEADER_MAX + FT_AP_BODY_MAX) break;
            got = recv(fd, buffer+used, FT_AP_HEADER_MAX+FT_AP_BODY_MAX-used, 0);
            if (got <= 0) break;
            used += (size_t)got;
            continue;
        }
        if (result != FT_AP_OK) {
            const char *bad = "RTSP/1.0 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            ++s_rejected;
            probe_send(fd, bad, strlen(bad));
            break;
        }
        {
            char response[256];
            int ok = !strcmp(req.method, "OPTIONS"), n;
            ++s_requests; ++requests;
            /* Unsupported authentication/audio always fails closed. */
            n = snprintf(response, sizeof(response),
                "%s %s\r\nCSeq: %lu\r\nServer: FeatherTalk-Transport-Probe\r\n"
                "%sContent-Length: 0\r\n\r\n",
                req.http ? "HTTP/1.1" : "RTSP/1.0",
                ok ? "200 OK" : "501 Not Implemented", (unsigned long)req.cseq,
                ok ? "Public: OPTIONS\r\n" : "");
            if (n < 0 || (size_t)n >= sizeof(response) || probe_send(fd, response, n)) break;
        }
        used -= req.consumed;
        memmove(buffer, buffer+req.consumed, used);
    }
}
static void probe_worker(void *unused)
{
    uint8_t *buffer = rt_malloc(FT_AP_HEADER_MAX + FT_AP_BODY_MAX);
    struct sockaddr_in address;
    int fd = -1;
    (void)unused;
    if (!buffer) { s_error = -RT_ENOMEM; goto done; }
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { s_error = -RT_EIO; goto done; }
    {
        int reuse = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) {
            s_error = -RT_EIO; goto done;
        }
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_port = htons(AP_PROBE_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 1)) {
        s_error = -RT_EIO; goto done;
    }
    rt_kprintf("[airplay] transport probe :7000; NOT a playable AirPlay service\n");
    while (!s_stop) {
        fd_set readfds;
        struct timeval timeout = {0, 200000};
        int ready;
        FD_ZERO(&readfds); FD_SET(fd, &readfds);
        ready = select(fd+1, &readfds, RT_NULL, RT_NULL, &timeout);
        if (ready < 0) { s_error = -RT_EIO; break; }
        if (ready > 0) {
            int client = accept(fd, RT_NULL, RT_NULL);
            if (client >= 0) { probe_client(client, buffer); closesocket(client); }
        }
    }
done:
    if (fd >= 0) closesocket(fd);
    if (buffer) { memset(buffer, 0, FT_AP_HEADER_MAX+FT_AP_BODY_MAX); rt_free(buffer); }
    s_active = 0;
}
static void ft_airplay(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "probe")) {
        ft_wifi_radio_t wifi;
        rt_thread_t thread;
        rt_base_t level;
        feathertalk_wifi_radio(&wifi);
        if (!wifi.ready) { rt_kprintf("[airplay] Wi-Fi not connected\n"); return; }
        level = rt_hw_interrupt_disable();
        if (s_active) { rt_hw_interrupt_enable(level); goto status; }
        s_active = 1; s_stop = 0; s_error = 0;
        rt_hw_interrupt_enable(level);
        thread = rt_thread_create("ap_probe", probe_worker, RT_NULL, 4096, 18, 10);
        if (!thread) { s_error = -RT_ENOMEM; s_active = 0; }
        else if (rt_thread_startup(thread) != RT_EOK) {
            rt_thread_delete(thread); s_error = -RT_ERROR; s_active = 0;
        }
    } else if (argc == 2 && !strcmp(argv[1], "stop")) s_stop = 1;
status:
    rt_kprintf("[airplay] stage=transport-foundation playable=0 advertised=0 probe=%d stopping=%d error=%d requests=%u rejected=%u audio-owner=%d\n",
        s_active, s_stop, s_error, s_requests, s_rejected, ft_audio_get_output_owner());
}
MSH_CMD_EXPORT(ft_airplay, AirPlay development status / probe / stop; no discovery or playback yet);
