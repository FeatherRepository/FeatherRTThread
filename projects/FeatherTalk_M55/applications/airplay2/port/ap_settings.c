#include <rtthread.h>
#include <rtdevice.h>
#include <wlan_mgnt.h>
#include "esp_mac.h"
#include "wifi.h"
#include "settings.h"
#include "dacp_client.h"
#include "feathertalk_wifi.h"
static float volume=-12.0f;
esp_err_t esp_read_mac(uint8_t *mac,int kind) { (void)kind; return rt_wlan_get_mac(mac)==RT_EOK?ESP_OK:ESP_FAIL; }
void wifi_get_mac_str(char *s,size_t n) { uint8_t m[6]={0}; esp_read_mac(m,0); rt_snprintf(s,n,"%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]); }
esp_err_t wifi_get_ip_str(char *s,size_t n) { ft_wifi_status_t *st=rt_malloc(sizeof(*st)); if(!st)return ESP_ERR_NO_MEM; feathertalk_wifi_status(st); rt_strncpy(s,st->ip,n-1); s[n-1]=0; int result=st->ready?ESP_OK:ESP_FAIL; rt_free(st); return result; }
bool wifi_is_connected(void) { ft_wifi_radio_t st; feathertalk_wifi_radio(&st); return st.ready; }
esp_err_t settings_get_device_name(char *s,size_t n) { rt_snprintf(s,n,"FeatherTalk AirPlay"); return ESP_OK; }
void settings_device_name_to_hostname(const char *s,char *out,size_t n) { (void)s; rt_snprintf(out,n,"feathertalk-airplay"); }
esp_err_t settings_get_volume(float *v) { *v=volume; return ESP_OK; }
esp_err_t settings_set_volume(float v) { volume=v; return ESP_OK; }
esp_err_t settings_persist_volume(void) { return ESP_ERR_NOT_SUPPORTED; }
/* Remote-control buttons/DACP are not offered; no network side effects. */
void dacp_set_session(const char *id,const char *token,uint32_t ip) { (void)id;(void)token;(void)ip; }
void dacp_clear_session(void) {}
bool dacp_probe_service(void) { return false; }
bool dacp_is_active(void) { return false; }
void dacp_send_playpause(void) {}
