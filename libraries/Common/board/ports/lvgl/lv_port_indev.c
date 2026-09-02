/*******************************************************************************
* File Name        : lv_port_indev.c
*
* Description      : This file provides implementation of low level input device
*                    driver for LVGL.
*
* Related Document : See README.md
*
******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "lv_port_indev.h"
#include "lv_port_disp.h"
#include "cy_utils.h"
#include "drv_touch.h"
#include "cybsp.h"

/*******************************************************************************
* Global Variables
*******************************************************************************/
lv_indev_t *indev_touchpad;

#define TOUCH_PHYS_HOR_RES ((int32_t)BSP_LCD_PHYSICAL_HOR_RES)
#define TOUCH_PHYS_VER_RES ((int32_t)BSP_LCD_PHYSICAL_VER_RES)
#define TOUCH_SAMPLE_PERIOD_MS  20U
#define TOUCH_THREAD_STACK_SIZE 2048U
#define TOUCH_THREAD_PRIORITY   ((RT_THREAD_PRIORITY_MAX * 2U / 3U) + 1U)

typedef struct
{
    volatile uint32_t sequence;
    volatile rt_bool_t pressed;
    volatile rt_int16_t x;
    volatile rt_int16_t y;
} touch_sample_cache_t;

static struct rt_thread touch_sample_thread;
static rt_uint8_t touch_sample_stack[TOUCH_THREAD_STACK_SIZE];
static touch_sample_cache_t touch_sample_cache;
static rt_bool_t touch_sample_thread_ready;
static rt_uint16_t touch_raw_hor_res = TOUCH_PHYS_HOR_RES;
static rt_uint16_t touch_raw_ver_res = TOUCH_PHYS_VER_RES;

static void touch_sample_publish(rt_bool_t pressed, rt_int16_t x, rt_int16_t y)
{
    touch_sample_cache.sequence++;
    __DMB();
    touch_sample_cache.pressed = pressed;
    if (pressed)
    {
        touch_sample_cache.x = x;
        touch_sample_cache.y = y;
    }
    __DMB();
    touch_sample_cache.sequence++;
}

static rt_bool_t touch_sample_snapshot(rt_int16_t *x, rt_int16_t *y)
{
    uint32_t before;
    uint32_t after;
    rt_bool_t pressed;

    for (;;)
    {
        before = touch_sample_cache.sequence;
        if ((before & 1U) != 0U) continue;
        __DMB();
        pressed = touch_sample_cache.pressed;
        *x = touch_sample_cache.x;
        *y = touch_sample_cache.y;
        __DMB();
        after = touch_sample_cache.sequence;
        if (before == after && (after & 1U) == 0U) break;
    }

    return pressed;
}

static void touch_sample_thread_entry(void *parameter)
{
    rt_int16_t x = 0;
    rt_int16_t y = 0;
    (void)parameter;

    while (1)
    {
        if (ST7102_get_single_touch(&x, &y) == RT_EOK)
            touch_sample_publish(RT_TRUE, x, y);
        else
            touch_sample_publish(RT_FALSE, x, y);
        rt_thread_mdelay(TOUCH_SAMPLE_PERIOD_MS);
    }
}

static int32_t touch_clamp(int32_t value, int32_t min, int32_t max)
{
    if (value < min)
    {
        return min;
    }
    if (value > max)
    {
        return max;
    }
    return value;
}

static int32_t touch_scale_axis(int32_t value, rt_uint16_t raw_extent,
                                int32_t physical_extent)
{
    if (value < 0) value = 0;
    if (raw_extent > 1U && value >= raw_extent) value = raw_extent - 1U;
    if (raw_extent > 1U && physical_extent > 1 &&
        raw_extent != (rt_uint16_t)physical_extent)
    {
        value = (value * (physical_extent - 1) + (raw_extent - 1U) / 2U) /
                (raw_extent - 1U);
    }
    return value;
}

static void touchpad_transform_point(rt_int16_t raw_x, rt_int16_t raw_y, lv_point_t *point)
{
    int32_t x;
    int32_t y;
    int32_t physical_x = touch_scale_axis(raw_x, touch_raw_hor_res,
                                          TOUCH_PHYS_HOR_RES);
    int32_t physical_y = touch_scale_axis(raw_y, touch_raw_ver_res,
                                          TOUCH_PHYS_VER_RES);

#if defined(BSP_LCD_ROTATION_DEGREES) && (BSP_LCD_ROTATION_DEGREES == 90)
    x = physical_y;
    y = (TOUCH_PHYS_HOR_RES - 1) - physical_x;
#elif defined(BSP_LCD_ROTATION_DEGREES) && (BSP_LCD_ROTATION_DEGREES == 180)
    x = (TOUCH_PHYS_HOR_RES - 1) - physical_x;
    y = (TOUCH_PHYS_VER_RES - 1) - physical_y;
#elif defined(BSP_LCD_ROTATION_DEGREES) && (BSP_LCD_ROTATION_DEGREES == 270)
    x = (TOUCH_PHYS_VER_RES - 1) - physical_y;
    y = physical_x;
#else
    x = physical_x;
    y = physical_y;
#endif

    point->x = touch_clamp(x, 0, MY_DISP_HOR_RES - 1);
    point->y = touch_clamp(y, 0, MY_DISP_VER_RES - 1);
}

/*******************************************************************************
* Function Name: touchpad_init
********************************************************************************
* Summary:
*  Initialization function for touchpad supported by LittelvGL.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void touchpad_init(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    rt_err_t thread_result;

    if (rt_hw_ST7102_port() != result)
    {
        CY_ASSERT(0);
    }

    if (ST7102_get_resolution(&touch_raw_hor_res,
                              &touch_raw_ver_res) != RT_EOK)
    {
        touch_raw_hor_res = TOUCH_PHYS_HOR_RES;
        touch_raw_ver_res = TOUCH_PHYS_VER_RES;
    }

    thread_result = rt_thread_init(&touch_sample_thread,
                                   "touch",
                                   touch_sample_thread_entry,
                                   RT_NULL,
                                   touch_sample_stack,
                                   sizeof(touch_sample_stack),
                                   TOUCH_THREAD_PRIORITY,
                                   5U);
    if (thread_result == RT_EOK)
    {
        touch_sample_thread_ready = RT_TRUE;
        rt_thread_startup(&touch_sample_thread);
    }
    else
    {
        touch_sample_thread_ready = RT_FALSE;
    }
}


/*******************************************************************************
* Function Name: touchpad_read
********************************************************************************
* Summary:
*  Touchpad read function called by the LVGL library.
*  Here you will find example implementation of input devices supported by
*  LittelvGL:
*   - Touchpad
*   - Mouse (with cursor support)
*   - Keypad (supports GUI usage only with key)
*   - Encoder (supports GUI usage only with: left, right, push)
*   - Button (external buttons to press points on the screen)
*
*   The `..._read()` function are only examples.
*   You should shape them according to your hardware.
*
*
* Parameters:
*  *indev_drv: Pointer to the input driver structure to be registered by HAL.
*  *data: Pointer to the data buffer holding touch coordinates.
*
* Return:
*  void
*
*******************************************************************************/
static void touchpad_read(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
    static lv_point_t last_point = {0, 0};
    rt_int16_t touch_x = 0;
    rt_int16_t touch_y = 0;
    cy_rslt_t result = CY_RSLT_SUCCESS;

    (void)indev_drv;

    if (touch_sample_thread_ready)
    {
        result = touch_sample_snapshot(&touch_x, &touch_y) ?
                 CY_RSLT_SUCCESS : (cy_rslt_t)-RT_ERROR;
    }
    else
    {
        result = ST7102_get_single_touch(&touch_x, &touch_y);
    }

    data->state = LV_INDEV_STATE_REL;
    if (CY_RSLT_SUCCESS == result)
    {
        touchpad_transform_point(touch_x, touch_y, &last_point);
        data->state = LV_INDEV_STATE_PR;
    }
    /* Set the last pressed coordinates */
    data->point = last_point;
}


/*******************************************************************************
* Function Name: lv_port_indev_init
********************************************************************************
* Summary:
*  Initialization function for input devices supported by LittelvGL.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void lv_port_indev_init(void)
{
    /* Initialize your touchpad if you have. */
    touchpad_init();

    /* Register a touchpad input device */
    indev_touchpad = lv_indev_create();
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);
}


/* [] END OF FILE */
