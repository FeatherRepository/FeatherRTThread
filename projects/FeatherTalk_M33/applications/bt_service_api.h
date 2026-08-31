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

/* 1 once the selected stack reports READY, 0 otherwise. */
int bt_service_enabled(void);

#endif
