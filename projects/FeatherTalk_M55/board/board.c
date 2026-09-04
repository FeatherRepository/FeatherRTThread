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
    ft_radio_attach(FT_RADIO_CORE_M55);
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

//Mos管控制
#define ES8388_CTRL                 GET_PIN(16, 2)  //ES8388 电源 Enable引脚
#define SPEAKER_OE_CTRL             GET_PIN(21, 6)  //功放 Enable引脚
#define LCD_BL_GPIO_NUM             GET_PIN(15, 7)  //LCD 背光电源开关
#define LCD_DISP_GPIO_NUM           GET_PIN(15, 6)  //LCD IC电源开关
#define BL_PWM_DISP_CTRL            GET_PIN(20, 6)  //LCD PWM亮度调节
#undef LCD_BL_GPIO_NUM
#undef LCD_DISP_GPIO_NUM
#undef BL_PWM_DISP_CTRL
#define LCD_BL_GPIO_NUM             GET_PIN(15, 7)
#define LCD_DISP_GPIO_NUM           GET_PIN(15, 6)
#define BL_PWM_DISP_CTRL            GET_PIN(20, 6)
#define LCD_POWER_STABLE_DELAY_MS   200U
#define LCD_CODEC_OFF_DELAY_MS      50U
int en_gpio(void)
{
    /* Shared rails and radio reset lines belong to the resource manager.
     * This sequence is now strictly LCD/codec/amplifier initialization. */
    rt_pin_mode(ES8388_CTRL, PIN_MODE_OUTPUT);
    rt_pin_mode(SPEAKER_OE_CTRL, PIN_MODE_OUTPUT);

    rt_pin_mode(BL_PWM_DISP_CTRL, PIN_MODE_OUTPUT);
    rt_pin_mode(LCD_DISP_GPIO_NUM, PIN_MODE_OUTPUT);
    rt_pin_mode(LCD_BL_GPIO_NUM, PIN_MODE_OUTPUT);

    rt_pin_write(LCD_BL_GPIO_NUM, PIN_LOW);
    rt_pin_write(LCD_DISP_GPIO_NUM, PIN_LOW);
    rt_pin_write(BL_PWM_DISP_CTRL, PIN_LOW);
    rt_pin_write(SPEAKER_OE_CTRL, PIN_LOW);
    rt_pin_write(ES8388_CTRL, PIN_LOW);
    Cy_SysLib_Delay(LCD_CODEC_OFF_DELAY_MS);
#ifdef BSP_USING_AUDIO
    rt_pin_write(ES8388_CTRL, PIN_HIGH);
#else
    rt_pin_write(ES8388_CTRL, PIN_LOW);
#endif
    /* The codec driver raises the amplifier only after ES8388 is configured.
     * Keeping it disabled during the board power sequence avoids a boot pop. */
    rt_pin_write(SPEAKER_OE_CTRL, PIN_LOW);
    rt_pin_write(BL_PWM_DISP_CTRL, PIN_LOW);
    rt_pin_write(LCD_BL_GPIO_NUM, PIN_LOW);
    rt_pin_write(LCD_DISP_GPIO_NUM, PIN_HIGH);
    Cy_SysLib_Delay(LCD_POWER_STABLE_DELAY_MS);

    return 0;
}
INIT_BOARD_EXPORT(en_gpio);
