/*
 * Copyright (c) 2026 FeatherTalk contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FEATHERTALK_BT_CONTROLLER_H
#define FEATHERTALK_BT_CONTROLLER_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the controller bring-up in a background RT-Thread thread. */
rt_err_t feathertalk_bt_controller_start(void);

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_BT_CONTROLLER_H */
