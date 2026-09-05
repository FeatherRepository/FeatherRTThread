/*
 * Copyright (c) 2026 FeatherTalk contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Stack-neutral Bluetooth service API.
 *
 * The product selects exactly one host stack at build time
 * (FEATHERTALK_BT_STACK_BK or FEATHERTALK_BT_STACK_AIROC); each stack
 * provides this implementation so IPC/UI code never touches
 * stack-specific symbols. */

#ifndef FEATHERTALK_BT_SERVICE_API_H
#define FEATHERTALK_BT_SERVICE_API_H

#include <rtthread.h>

/* Idempotent: start the selected host stack (power, PatchRAM, stack init). */
rt_err_t bt_service_start(void);
/* Nonblocking desired state. Hardware work is serialized by the service. */
rt_err_t bt_service_set_enabled(int on);
int bt_service_busy(void);
int bt_service_target(void);
int bt_service_error(void);

/* 1 once the selected stack reports READY, 0 otherwise. */
int bt_service_enabled(void);

/* 1 while a BLE or Classic peer connection is active, 0 otherwise.
   (Stacks without connection support return 0.) */
int bt_service_connected(void);

/* M6-BT: A2DP 角色仲裁 (A2 单角色)。0=SINK(音箱,被动可连) 1=SOURCE(转发)。
 * 未上电时返回 -RT_EBUSY; 未知角色返回 -RT_EINVAL。 */
rt_err_t bt_service_set_a2dp_role(int role);
int bt_service_a2dp_role(void);

#endif
