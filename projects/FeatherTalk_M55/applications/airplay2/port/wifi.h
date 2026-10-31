#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
void wifi_get_mac_str(char *s,size_t n);
esp_err_t wifi_get_ip_str(char *s,size_t n);
bool wifi_is_connected(void);
