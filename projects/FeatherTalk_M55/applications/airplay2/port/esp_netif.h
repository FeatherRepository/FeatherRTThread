#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip,netmask,gw; } esp_netif_ip_info_t;
typedef int esp_netif_t;
/* Only legacy Apple-Challenge uses this path; legacy RSA mode is not supported. */
static inline esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) { (void)key;return 0; }
static inline esp_err_t esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *i) { (void)n;(void)i;return ESP_ERR_NOT_SUPPORTED; }
