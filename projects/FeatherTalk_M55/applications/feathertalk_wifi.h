#ifndef FEATHERTALK_WIFI_H
#define FEATHERTALK_WIFI_H
#include <stdbool.h>
#include <stdint.h>

#define FT_WIFI_MAX_NETWORKS 32
#define FT_WIFI_SSID_BYTES 33
#define FT_WIFI_KEY_BYTES 65
typedef enum {
    FT_WIFI_WAITING, FT_WIFI_OFF, FT_WIFI_IDLE, FT_WIFI_SCANNING,
    FT_WIFI_CONNECTING, FT_WIFI_ADDRESS, FT_WIFI_CONNECTED, FT_WIFI_ERROR
} ft_wifi_state_t;
typedef struct {
    char ssid[FT_WIFI_SSID_BYTES];
    int16_t rssi, channel;
    uint8_t band;
    uint8_t bssid[6];
    uint32_t security;
} ft_wifi_network_t;
typedef struct {
    uint32_t revision, scan_revision;
    ft_wifi_state_t state;
    bool available, enabled, associated, ready, busy;
    int error, rssi;
    uint8_t signal, count;
    char ssid[FT_WIFI_SSID_BYTES], ip[16], gateway[16], mac[18];
    ft_wifi_network_t networks[FT_WIFI_MAX_NETWORKS];
} ft_wifi_status_t;
typedef struct {
    bool available, enabled, associated, ready, busy;
    uint8_t signal;
} ft_wifi_radio_t;

void feathertalk_wifi_status(ft_wifi_status_t *status);
void feathertalk_wifi_radio(ft_wifi_radio_t *radio);
int feathertalk_wifi_enable(bool enable);
int feathertalk_wifi_scan(void);
int feathertalk_wifi_connect(const char *ssid, const char *password);
int feathertalk_wifi_disconnect(void);
#endif
