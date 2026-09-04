/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-06-29     Rbb666       first version
 * 2025-08-20     Hydevcode
 */

#include "board.h"
#include <feathertalk/radio_manager.h>
#define ES8388_CTRL             GET_PIN(16, 2)
#define SPEAKER_OE_CTRL         GET_PIN(21, 6)

void cy_bsp_all_init(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    if (CY_SYSLIB_RESET_HIB_WAKEUP == (Cy_SysLib_GetResetReason() &
                                       CY_SYSLIB_RESET_HIB_WAKEUP))
    {
        Cy_SysLib_ClearResetReason();
        Cy_SysPm_IoUnfreeze();
        Cy_SysPm_TriggerXRes();

    }
    /* Board-resource initialization does not depend on either radio feature.
     * Complete shared power setup before the other application core starts. */
    ft_radio_board_boot();
    ft_radio_attach(FT_RADIO_CORE_M33);
#ifdef SOC_Enable_CM55
    Cy_SysEnableCM55(MXCM55, CY_CM55_APP_BOOT_ADDR, 10);
#ifdef SOC_Enable_CM33_DeepSleep
    while (1)
    {
        Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
    }
#endif
#endif

}

void _start(void)
{
    extern int entry(void);
    entry();
    while (1);
    __builtin_unreachable();
}

void poweroff(void)
{
    rt_pin_mode(ES8388_CTRL, PIN_MODE_OUTPUT);
    rt_pin_write(ES8388_CTRL, PIN_LOW);

    rt_pin_mode(SPEAKER_OE_CTRL, PIN_MODE_OUTPUT);
    rt_pin_write(SPEAKER_OE_CTRL, PIN_LOW);

    /* Disable audio loads before removing the shared baseboard supply. */
    ft_radio_system_poweroff();
    Cy_SysClk_PllDisable(SRSS_DPLL_LP_0_PATH_NUM);
    Cy_SysPm_SystemEnterHibernate();
}

#ifdef RT_USING_MSH
    MSH_CMD_EXPORT(poweroff, The software enables the system to shut down. Simply press the button to restart it.);
#endif
