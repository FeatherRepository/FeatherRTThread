/* bt_service.h - 蓝牙服务对外接口 (M33 侧, 供 IPC 快捷控制调用)
 *
 * 栈中立部分 (bt_service_start / bt_service_enabled) 已迁移到
 * projects/FeatherTalk_M33/applications/bt_service_api.h;
 * 本头保留 BK 栈特有的旧接口。 */
#ifndef BT_SERVICE_H__
#define BT_SERVICE_H__

#include <stdint.h>
#include "bt_service_api.h"

/* 开/关蓝牙电源与 HCI (value: 1=on, 0=off)
 * 返回 0 = 请求已接受或已是目标状态；完成需查询 busy/enabled/error。 */
int bt_service_quick_control(uint8_t on);

/* 最近一次 bring-up 失败码 (0=正常/未启动, 9x=各阶段失败, 见 bt_main.c) */
int bt_service_error(void);
/* Queue a nonblocking stack diagnostic on the sole HCI owner thread. */
int bt_service_run_callback(void (*callback)(void));
#endif
