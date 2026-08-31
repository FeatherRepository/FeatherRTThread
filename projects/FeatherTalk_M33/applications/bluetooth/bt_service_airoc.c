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
