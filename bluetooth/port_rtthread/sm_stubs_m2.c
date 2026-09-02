/* sm_stubs_m2.c - M2 阶段 SM(安全管理)最小桩实现
 *
 * att_server 在无配对/无加密特性的 GATT 服务下, 只在安全相关路径调用这些
 * 接口; M2 不提供配对能力, 全部按"无安全上下文"语义返回。
 * M3 引入 sm.c + gap.c + btstack_crypto 后本文件必须移出构建!
 */
#include <stdint.h>
#include <stdbool.h>
#include "hci.h"

/* gap.h: bool gap_reconnect_security_setup_active(hci_con_handle_t) */
bool gap_reconnect_security_setup_active(hci_con_handle_t con_handle)
{
    (void)con_handle;
    return false;   /* 无重连安全建立 */
}

/* sm.h: void sm_add_event_handler(btstack_packet_callback_registration_t *) */
void sm_add_event_handler(void *callback_handler)
{
    (void)callback_handler;   /* 无 SM 事件源 */
}

/* sm.h: void sm_request_pairing(hci_con_handle_t) */
void sm_request_pairing(hci_con_handle_t con_handle)
{
    (void)con_handle;   /* M2 特性均不要求加密, 此路径不应到达 */
}

/* sm.h: int sm_le_device_index(hci_con_handle_t) */
int sm_le_device_index(hci_con_handle_t con_handle)
{
    (void)con_handle;
    return -1;   /* 未解析到设备 */
}
