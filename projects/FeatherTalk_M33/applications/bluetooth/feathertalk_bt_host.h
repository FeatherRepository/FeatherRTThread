/*
 * Copyright (c) 2026 FeatherTalk contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FEATHERTALK_BT_HOST_H
#define FEATHERTALK_BT_HOST_H

#include <rtthread.h>

rt_err_t feathertalk_bt_host_start(void);

/* IPC quick-status view: 1 once the AIROC host stack is READY. */
int feathertalk_bt_host_enabled(void);

#endif
