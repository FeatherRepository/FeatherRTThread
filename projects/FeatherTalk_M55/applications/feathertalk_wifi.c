#include "feathertalk_wifi.h"
#include <rtthread.h>
#include <feathertalk/radio_manager.h>
#ifdef FEATHERTALK_USING_WIFI
#include <rtdevice.h>
#include <wlan_mgnt.h>
#include <lwip/ip_addr.h>
#include <netdev.h>
#include <string.h>

#if !defined(WHD_RESOURCES_IN_MEMORY)
#error "FeatherTalk Wi-Fi resources must be embedded; demo FAL partitions overlap the user disk"
#endif
#if RT_SDIO_STACK_SIZE < 2048
#error "FeatherTalk WHD needs at least 2 KiB SDIO IRQ stack; the SDK 512-byte default overflows"
#endif

extern rt_bool_t feathertalk_whd_ready(void);
extern int feathertalk_whd_enable(rt_bool_t enabled);
enum { WIFI_ENABLE = 1, WIFI_DISABLE, WIFI_SCAN, WIFI_CONNECT, WIFI_DISCONNECT };
typedef struct {
    uint8_t command;
    char ssid[FT_WIFI_SSID_BYTES], key[FT_WIFI_KEY_BYTES];
} wifi_request_t;
static struct rt_mutex s_lock;
static rt_mq_t s_requests;
static ft_wifi_status_t s_status;
static bool s_started;

void feathertalk_wifi_radio(ft_wifi_radio_t *radio)
{
    memset(radio, 0, sizeof(*radio));
    if (!s_started) return;
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    radio->available = s_status.available; radio->enabled = s_status.enabled;
    radio->associated = s_status.associated; radio->ready = s_status.ready;
    radio->busy = s_status.busy; radio->signal = s_status.signal;
    rt_mutex_release(&s_lock);
}

void feathertalk_wifi_status(ft_wifi_status_t *status)
{
    if (!status) return;
    if (!s_started) { memset(status, 0, sizeof(*status)); return; }
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    *status = s_status;
    rt_mutex_release(&s_lock);
}

static int request(uint8_t command, const char *ssid, const char *password)
{
    wifi_request_t item = {0};
    int result;
    if (!s_started || !s_requests) return -RT_EBUSY;
    if (ssid && (strlen(ssid) == 0 || strlen(ssid) >= sizeof(item.ssid))) return -RT_EINVAL;
    if (password && strlen(password) >= sizeof(item.key)) return -RT_EINVAL;
    item.command = command;
    if (ssid) memcpy(item.ssid, ssid, strlen(ssid));
    if (password) memcpy(item.key, password, strlen(password));
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    if (!s_status.available || s_status.busy ||
        (!s_status.enabled && command != WIFI_ENABLE && command != WIFI_DISABLE))
        result = -RT_EBUSY;
    else {
        result = rt_mq_send(s_requests, &item, sizeof(item));
        if (result == RT_EOK) {
            s_status.busy = true;
            s_status.error = 0;
            if (command == WIFI_SCAN) s_status.state = FT_WIFI_SCANNING;
            if (command == WIFI_CONNECT) s_status.state = FT_WIFI_CONNECTING;
            s_status.revision++;
        }
    }
    rt_mutex_release(&s_lock);
    memset(item.key, 0, sizeof(item.key));
    return result;
}
int feathertalk_wifi_enable(bool enable) { return request(enable ? WIFI_ENABLE : WIFI_DISABLE, NULL, NULL); }
int feathertalk_wifi_scan(void) { return request(WIFI_SCAN, NULL, NULL); }
int feathertalk_wifi_connect(const char *ssid, const char *key) { return request(WIFI_CONNECT, ssid, key); }
int feathertalk_wifi_disconnect(void) { return request(WIFI_DISCONNECT, NULL, NULL); }

static void scan_report(int event, struct rt_wlan_buff *buff, void *parameter)
{
    struct rt_wlan_info *info;
    unsigned i;
    (void)event; (void)parameter;
    if (!buff || !buff->data || buff->len < sizeof(*info)) return;
    info = buff->data;
    if (info->ssid.len <= 0 || info->ssid.len > 32) return;
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    for (i = 0; i < s_status.count; i++)
        if (!memcmp(s_status.networks[i].bssid, info->bssid, 6)) break;
    if (i < FT_WIFI_MAX_NETWORKS) {
        ft_wifi_network_t *out = &s_status.networks[i];
        if (i == s_status.count) s_status.count++;
        memset(out, 0, sizeof(*out));
        memcpy(out->ssid, info->ssid.val, info->ssid.len);
        memcpy(out->bssid, info->bssid, 6);
        out->rssi = info->rssi; out->channel = info->channel;
        out->security = info->security;
    }
    rt_mutex_release(&s_lock);
}

/* Only the worker calls WLAN/ioctl APIs. UI and IPC only read a short snapshot. */
static void update_connection(void)
{
    bool associated = rt_wlan_is_connected(), ready = rt_wlan_is_ready();
    struct rt_wlan_info info;
    /* wlan0 is the radio device; its lwIP netdev is named w0 by the SDK.
     * Resolve the registered protocol binding rather than hard-code either. */
    struct rt_wlan_device *wlan = (struct rt_wlan_device *)rt_device_find(RT_WLAN_DEVICE_STA_NAME);
    struct netdev *net = wlan ? wlan->netdev : RT_NULL;
    uint8_t mac[6];
    int rssi = associated ? rt_wlan_get_rssi() : -100;
    char ip[16] = "", gateway[16] = "", ssid[FT_WIFI_SSID_BYTES] = "", address[18] = "";
    int signal = (rssi + 100) * 2;
    if (signal < 0) signal = 0;
    if (signal > 100) signal = 100;
    if (associated && rt_wlan_get_info(&info) == RT_EOK && info.ssid.len > 0 && info.ssid.len <= 32)
        memcpy(ssid, info.ssid.val, info.ssid.len);
    if (net && ready) {
        ipaddr_ntoa_r(&net->ip_addr, ip, sizeof(ip));
        ipaddr_ntoa_r(&net->gw, gateway, sizeof(gateway));
    }
    if (rt_wlan_get_mac(mac) == RT_EOK)
        rt_snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    if (!s_status.enabled) {
        associated = ready = false;
        ssid[0] = ip[0] = gateway[0] = '\0';
        rssi = -100; signal = 0;
    }
    s_status.associated = associated; s_status.ready = ready;
    s_status.rssi = rssi; s_status.signal = signal;
    memcpy(s_status.ssid, ssid, sizeof(ssid));
    memcpy(s_status.ip, ip, sizeof(ip)); memcpy(s_status.gateway, gateway, sizeof(gateway));
    memcpy(s_status.mac, address, sizeof(address));
    if (!s_status.busy)
        s_status.state = !s_status.enabled ? FT_WIFI_OFF : s_status.error ? FT_WIFI_ERROR :
                         ready ? FT_WIFI_CONNECTED : associated ? FT_WIFI_ADDRESS : FT_WIFI_IDLE;
    s_status.revision++;
    rt_mutex_release(&s_lock);
}

static void worker(void *parameter)
{
    wifi_request_t item;
    (void)parameter;
    while (!feathertalk_whd_ready()) rt_thread_mdelay(200);
    rt_wlan_config_autoreconnect(RT_FALSE);
    rt_wlan_register_event_handler(RT_WLAN_EVT_SCAN_REPORT, scan_report, RT_NULL);
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    s_status.available = true; s_status.enabled = true; s_status.state = FT_WIFI_IDLE;
    s_status.revision++;
    rt_mutex_release(&s_lock);
    rt_kprintf("[wifi] service ready; use ft_wifi scan (no automatic scan/join)\n");
    while (1) {
        int result = RT_EOK;
        /* RT-Thread 5 returns the received byte count (not RT_EOK). */
        if (rt_mq_recv(s_requests, &item, sizeof(item), rt_tick_from_millisecond(1000)) == sizeof(item)) {
            switch (item.command) {
            case WIFI_ENABLE:
                result = s_status.enabled ? RT_EOK : feathertalk_whd_enable(RT_TRUE);
                break;
            case WIFI_DISABLE:
                if (!s_status.enabled) break; /* Already down: no repeated firmware control. */
                rt_wlan_config_autoreconnect(RT_FALSE);
                if (rt_wlan_is_connected()) result = rt_wlan_disconnect();
                if (result == RT_EOK) result = feathertalk_whd_enable(RT_FALSE);
                break;
            case WIFI_SCAN:
                rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
                s_status.count = 0;
                rt_mutex_release(&s_lock);
                result = rt_wlan_scan_with_info(RT_NULL);
                rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
                s_status.scan_revision++;
                rt_mutex_release(&s_lock);
                break;
            case WIFI_CONNECT:
                result = rt_wlan_connect(item.ssid, item.key);
                break;
            case WIFI_DISCONNECT:
                rt_wlan_config_autoreconnect(RT_FALSE);
                result = rt_wlan_is_connected() ? rt_wlan_disconnect() : RT_EOK;
                break;
            }
            memset(item.key, 0, sizeof(item.key));
            if (item.command == WIFI_ENABLE || item.command == WIFI_DISABLE) {
                ft_radio_set_state(FT_RADIO_WIFI, result != RT_EOK ? FT_RADIO_ERROR :
                                   item.command == WIFI_ENABLE ? FT_RADIO_READY : FT_RADIO_QUIESCED,
                                   result);
            }
            rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
            if (result == RT_EOK && (item.command == WIFI_ENABLE || item.command == WIFI_DISABLE))
                s_status.enabled = item.command == WIFI_ENABLE;
            if (!s_status.enabled) {
                s_status.associated = s_status.ready = false;
                s_status.ssid[0] = s_status.ip[0] = s_status.gateway[0] = '\0';
            }
            s_status.state = !s_status.enabled ? FT_WIFI_OFF : result ? FT_WIFI_ERROR :
                s_status.ready ? FT_WIFI_CONNECTED : s_status.associated ? FT_WIFI_ADDRESS : FT_WIFI_IDLE;
            s_status.busy = false; s_status.error = result; s_status.revision++;
            rt_mutex_release(&s_lock);
            rt_kprintf("[wifi] operation=%u result=%d\n", item.command, result);
        }
        update_connection();
    }
}
static int wifi_init(void)
{
    rt_thread_t thread;
    rt_mutex_init(&s_lock, "ftwifi", RT_IPC_FLAG_PRIO);
    s_requests = rt_mq_create("ftwifi", sizeof(wifi_request_t), 2, RT_IPC_FLAG_PRIO);
    if (!s_requests) return -RT_ENOMEM;
    thread = rt_thread_create("ftwifi", worker, RT_NULL, 6144, 16, 10);
    if (!thread) { rt_mq_delete(s_requests); s_requests = RT_NULL; return -RT_ENOMEM; }
    s_started = true;
    return rt_thread_startup(thread);
}
INIT_APP_EXPORT(wifi_init);

static void ft_wifi(int argc, char **argv)
{
    static ft_wifi_status_t status;
    int result = RT_EOK;
    if (argc > 1 && !strcmp(argv[1], "scan")) result = feathertalk_wifi_scan();
    else if (argc > 1 && !strcmp(argv[1], "on")) result = feathertalk_wifi_enable(true);
    else if (argc > 1 && !strcmp(argv[1], "off")) result = feathertalk_wifi_enable(false);
    else if (argc > 1 && !strcmp(argv[1], "disconnect")) result = feathertalk_wifi_disconnect();
    else if (argc > 2 && !strcmp(argv[1], "join")) result = feathertalk_wifi_connect(argv[2], argc > 3 ? argv[3] : "");
    feathertalk_wifi_status(&status);
    rt_kprintf("[wifi] rc=%d ready=%d enabled=%d busy=%d state=%d error=%d country=%s\n",
               result, status.available, status.enabled, status.busy, status.state, status.error, WHD_COUNTRY_CODE);
    rt_kprintf("[wifi] ssid=%s ip=%s gw=%s mac=%s rssi=%d networks=%u\n",
               status.ssid, status.ip, status.gateway, status.mac, status.rssi, status.count);
    for (unsigned i = 0; i < status.count; i++)
        rt_kprintf("  %u: %s ch=%d rssi=%d security=0x%x\n", i, status.networks[i].ssid,
                    status.networks[i].channel, status.networks[i].rssi, status.networks[i].security);
}
MSH_CMD_EXPORT(ft_wifi, WiFi status / scan / on / off / join SSID key / disconnect);
#else
void feathertalk_wifi_radio(ft_wifi_radio_t *radio) { rt_memset(radio, 0, sizeof(*radio)); }
void feathertalk_wifi_status(ft_wifi_status_t *status) { if (status) rt_memset(status, 0, sizeof(*status)); }
int feathertalk_wifi_enable(bool enable) { (void)enable; return -RT_ENOSYS; }
int feathertalk_wifi_scan(void) { return -RT_ENOSYS; }
int feathertalk_wifi_connect(const char *ssid, const char *key) { (void)ssid; (void)key; return -RT_ENOSYS; }
int feathertalk_wifi_disconnect(void) { return -RT_ENOSYS; }
#endif
