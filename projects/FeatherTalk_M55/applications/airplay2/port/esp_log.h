#pragma once
#include <rtthread.h>
#include <inttypes.h>
#ifdef __cplusplus
extern "C" {
#endif
void ap_log(const char *level,const char *tag,const char *fmt,...);
void ap_diag_record(const char *stage,int value);
#ifdef __cplusplus
}
#endif
#define ESP_LOGE(tag, fmt, ...) ap_log("E",tag,fmt,##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) ap_log("W",tag,fmt,##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) ap_log("I",tag,fmt,##__VA_ARGS__)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define ESP_LOG_BUFFER_HEX(...) ((void)0)
#define ESP_LOG_BUFFER_HEXDUMP(...) ((void)0)
