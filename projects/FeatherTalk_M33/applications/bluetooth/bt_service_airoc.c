/*
 * Copyright (c) 2026 FeatherTalk contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* AIROC implementation of the stack-neutral Bluetooth service API.
 * Built only when FEATHERTALK_BT_STACK_AIROC is selected. */

#include "bt_service_api.h"

#include "feathertalk_bt_host.h"

rt_err_t bt_service_start(void)
{
    return feathertalk_bt_host_start();
}

int bt_service_enabled(void)
{
    return feathertalk_bt_host_enabled();
}

int bt_service_connected(void)
{
    /* The AIROC reference path currently supports scan/advertising only. */
    return 0;
}

rt_err_t bt_service_set_enabled(int on)
{
    if (on != 0 && on != 1) return -RT_EINVAL;
    return on ? bt_service_start() : -RT_ENOSYS;
}
int bt_service_busy(void) { return 0; }
int bt_service_target(void) { return bt_service_enabled(); }
int bt_service_error(void) { return 0; }
