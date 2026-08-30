/*
 * Copyright (c) 2026 FeatherTalk contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cyabs_rtos.h"

#include <limits.h>

static volatile cy_rtos_error_t g_last_error;
static uint32_t g_object_sequence;

static rt_int32_t ft_timeout_ticks(cy_time_t timeout_ms)
{
    rt_tick_t ticks;

    if (timeout_ms == CY_RTOS_NEVER_TIMEOUT)
    {
        return RT_WAITING_FOREVER;
    }
    if (timeout_ms == 0U)
    {
        return 0;
    }

    ticks = rt_tick_from_millisecond(timeout_ms);
    return (rt_int32_t)(ticks == 0U ? 1U : ticks);
}

static cy_rslt_t ft_result(rt_err_t result, cy_rslt_t timeout_result)
{
    g_last_error = result;
    if (result == RT_EOK)
    {
        return CY_RSLT_SUCCESS;
    }
    if (result == -RT_ETIMEOUT)
    {
        return timeout_result;
    }
    if (result == -RT_ENOMEM)
    {
        return CY_RTOS_NO_MEMORY;
    }
    return CY_RTOS_GENERAL_ERROR;
}

static void ft_object_name(char name[RT_NAME_MAX], const char *prefix)
{
    uint32_t sequence;

    sequence = g_object_sequence++;
    rt_snprintf(name, RT_NAME_MAX, "%s%lu", prefix, (unsigned long)(sequence % 1000U));
}

static rt_uint8_t ft_thread_priority(cy_thread_priority_t priority)
{
    static const rt_uint8_t priorities[] = {26U, 24U, 22U, 20U, 18U, 14U, 10U, 8U};
    uint32_t index = (uint32_t)priority;

    if (index >= (sizeof(priorities) / sizeof(priorities[0])))
    {
        index = (uint32_t)CY_RTOS_PRIORITY_NORMAL;
    }
    return priorities[index];
}

cy_rtos_error_t cy_rtos_last_error(void)
{
    return g_last_error;
}

cy_rslt_t cy_rtos_create_thread(cy_thread_t *thread,
                                cy_thread_entry_fn_t entry_function,
                                const char *name,
                                void *stack,
                                uint32_t stack_size,
                                cy_thread_priority_t priority,
                                cy_thread_arg_t arg)
{
    rt_thread_t created;

    if ((thread == RT_NULL) || (entry_function == RT_NULL) || (stack != RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    created = rt_thread_create(name != RT_NULL ? name : "cybt",
                               entry_function,
                               arg,
                               stack_size,
                               ft_thread_priority(priority),
                               10U);
    if (created == RT_NULL)
    {
        return CY_RTOS_NO_MEMORY;
    }

    *thread = created;
    return ft_result(rt_thread_startup(created), CY_RTOS_GENERAL_ERROR);
}

cy_rslt_t cy_rtos_exit_thread(void)
{
    /* RT-Thread tears down a dynamic thread when its entry function returns.
     * AIROC's task entry points call this at their tail and return directly. */
    return CY_RSLT_SUCCESS;
}

cy_rslt_t cy_rtos_join_thread(cy_thread_t *thread)
{
    /* RT-Thread reclaims dynamic thread objects when their entry returns. */
    (void)thread;
    return CY_RSLT_SUCCESS;
}

cy_rslt_t cy_rtos_get_thread_handle(cy_thread_t *thread)
{
    if (thread == RT_NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }
    *thread = rt_thread_self();
    return *thread != RT_NULL ? CY_RSLT_SUCCESS : CY_RTOS_GENERAL_ERROR;
}

cy_rslt_t cy_rtos_init_mutex(cy_mutex_t *mutex)
{
    char name[RT_NAME_MAX];
    if (mutex == RT_NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }
    ft_object_name(name, "btm");
    *mutex = rt_mutex_create(name, RT_IPC_FLAG_PRIO);
    return *mutex != RT_NULL ? CY_RSLT_SUCCESS : CY_RTOS_NO_MEMORY;
}

cy_rslt_t cy_rtos_get_mutex(cy_mutex_t *mutex, cy_time_t timeout_ms)
{
    if ((mutex == RT_NULL) || (*mutex == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    return ft_result(rt_mutex_take(*mutex, ft_timeout_ticks(timeout_ms)), CY_RTOS_TIMEOUT);
}

cy_rslt_t cy_rtos_set_mutex(cy_mutex_t *mutex)
{
    if ((mutex == RT_NULL) || (*mutex == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    return ft_result(rt_mutex_release(*mutex), CY_RTOS_GENERAL_ERROR);
}

cy_rslt_t cy_rtos_deinit_mutex(cy_mutex_t *mutex)
{
    rt_err_t result;
    if ((mutex == RT_NULL) || (*mutex == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    result = rt_mutex_delete(*mutex);
    *mutex = RT_NULL;
    return ft_result(result, CY_RTOS_GENERAL_ERROR);
}

cy_rslt_t cy_rtos_init_semaphore(cy_semaphore_t *semaphore,
                                 uint32_t maxcount,
                                 uint32_t initcount)
{
    char name[RT_NAME_MAX];
    (void)maxcount;
    if ((semaphore == RT_NULL) || (initcount > UINT16_MAX))
    {
        return CY_RTOS_BAD_PARAM;
    }
    ft_object_name(name, "bts");
    *semaphore = rt_sem_create(name, (rt_uint32_t)initcount, RT_IPC_FLAG_PRIO);
    return *semaphore != RT_NULL ? CY_RSLT_SUCCESS : CY_RTOS_NO_MEMORY;
}

cy_rslt_t cy_rtos_get_semaphore(cy_semaphore_t *semaphore,
                                cy_time_t timeout_ms,
                                bool in_isr)
{
    (void)in_isr;
    if ((semaphore == RT_NULL) || (*semaphore == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    return ft_result(rt_sem_take(*semaphore, ft_timeout_ticks(timeout_ms)), CY_RTOS_TIMEOUT);
}

cy_rslt_t cy_rtos_set_semaphore(cy_semaphore_t *semaphore, bool in_isr)
{
    (void)in_isr;
    if ((semaphore == RT_NULL) || (*semaphore == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    return ft_result(rt_sem_release(*semaphore), CY_RTOS_GENERAL_ERROR);
}

cy_rslt_t cy_rtos_deinit_semaphore(cy_semaphore_t *semaphore)
{
    rt_err_t result;
    if ((semaphore == RT_NULL) || (*semaphore == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    result = rt_sem_delete(*semaphore);
    *semaphore = RT_NULL;
    return ft_result(result, CY_RTOS_GENERAL_ERROR);
}

cy_rslt_t cy_rtos_init_queue(cy_queue_t *queue, size_t length, size_t itemsize)
{
    char name[RT_NAME_MAX];
    if ((queue == RT_NULL) || (length == 0U) || (itemsize == 0U))
    {
        return CY_RTOS_BAD_PARAM;
    }
    ft_object_name(name, "btq");
    *queue = rt_mq_create(name,
                          (rt_size_t)itemsize,
                          (rt_size_t)length,
                          RT_IPC_FLAG_PRIO);
    return *queue != RT_NULL ? CY_RSLT_SUCCESS : CY_RTOS_NO_MEMORY;
}

cy_rslt_t cy_rtos_put_queue(cy_queue_t *queue,
                            const void *item_ptr,
                            cy_time_t timeout_ms,
                            bool in_isr)
{
    rt_err_t result;
    (void)timeout_ms;
    if ((queue == RT_NULL) || (*queue == RT_NULL) || (item_ptr == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    result = in_isr ? rt_mq_send(*queue, item_ptr, (*queue)->msg_size)
                    : rt_mq_send_wait(*queue, item_ptr, (*queue)->msg_size,
                                      ft_timeout_ticks(timeout_ms));
    return ft_result(result, CY_RTOS_QUEUE_FULL);
}

cy_rslt_t cy_rtos_get_queue(cy_queue_t *queue,
                            void *item_ptr,
                            cy_time_t timeout_ms,
                            bool in_isr)
{
    rt_ssize_t result;
    if ((queue == RT_NULL) || (*queue == RT_NULL) || (item_ptr == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    result = rt_mq_recv(*queue,
                        item_ptr,
                        (*queue)->msg_size,
                        in_isr ? 0 : ft_timeout_ticks(timeout_ms));
    /* RT-Thread 5 returns the received byte count on success, while the
     * Infineon abstraction expects CY_RSLT_SUCCESS (zero). */
    if (result >= 0)
    {
        g_last_error = RT_EOK;
        return CY_RSLT_SUCCESS;
    }
    return ft_result((rt_err_t)result, CY_RTOS_QUEUE_EMPTY);
}

cy_rslt_t cy_rtos_count_queue(cy_queue_t *queue, size_t *num_waiting)
{
    if ((queue == RT_NULL) || (*queue == RT_NULL) || (num_waiting == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    *num_waiting = (size_t)(*queue)->entry;
    return CY_RSLT_SUCCESS;
}

cy_rslt_t cy_rtos_deinit_queue(cy_queue_t *queue)
{
    rt_err_t result;
    if ((queue == RT_NULL) || (*queue == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    result = rt_mq_delete(*queue);
    *queue = RT_NULL;
    return ft_result(result, CY_RTOS_GENERAL_ERROR);
}

cy_rslt_t cy_rtos_init_timer(cy_timer_t *timer,
                             cy_timer_trigger_type_t type,
                             cy_timer_callback_t callback,
                             cy_timer_callback_arg_t arg)
{
    char name[RT_NAME_MAX];
    rt_uint8_t flags;
    if ((timer == RT_NULL) || (callback == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    ft_object_name(name, "btt");
    flags = (type == CY_TIMER_TYPE_PERIODIC) ? RT_TIMER_FLAG_PERIODIC : RT_TIMER_FLAG_ONE_SHOT;
    flags |= RT_TIMER_FLAG_SOFT_TIMER;
    *timer = rt_timer_create(name, callback, arg, 1U, flags);
    return *timer != RT_NULL ? CY_RSLT_SUCCESS : CY_RTOS_NO_MEMORY;
}

cy_rslt_t cy_rtos_start_timer(cy_timer_t *timer, cy_time_t num_ms)
{
    rt_tick_t ticks;
    rt_err_t result;
    if ((timer == RT_NULL) || (*timer == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    ticks = rt_tick_from_millisecond(num_ms);
    if (ticks == 0U)
    {
        ticks = 1U;
    }
    result = rt_timer_control(*timer, RT_TIMER_CTRL_SET_TIME, &ticks);
    if (result != RT_EOK)
    {
        return ft_result(result, CY_RTOS_GENERAL_ERROR);
    }
    return ft_result(rt_timer_start(*timer), CY_RTOS_GENERAL_ERROR);
}

cy_rslt_t cy_rtos_stop_timer(cy_timer_t *timer)
{
    if ((timer == RT_NULL) || (*timer == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    return ft_result(rt_timer_stop(*timer), CY_RTOS_GENERAL_ERROR);
}

cy_rslt_t cy_rtos_is_running_timer(cy_timer_t *timer, bool *state)
{
    if ((timer == RT_NULL) || (*timer == RT_NULL) || (state == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    *state = (((*timer)->parent.flag & RT_TIMER_FLAG_ACTIVATED) != 0U);
    return CY_RSLT_SUCCESS;
}

cy_rslt_t cy_rtos_deinit_timer(cy_timer_t *timer)
{
    rt_err_t result;
    if ((timer == RT_NULL) || (*timer == RT_NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }
    result = rt_timer_delete(*timer);
    *timer = RT_NULL;
    return ft_result(result, CY_RTOS_GENERAL_ERROR);
}

cy_rslt_t cy_rtos_get_time(cy_time_t *tval)
{
    uint64_t milliseconds;
    if (tval == RT_NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }
    milliseconds = ((uint64_t)rt_tick_get() * 1000ULL) / (uint64_t)RT_TICK_PER_SECOND;
    *tval = (cy_time_t)milliseconds;
    return CY_RSLT_SUCCESS;
}

cy_rslt_t cy_rtos_delay_milliseconds(cy_time_t num_ms)
{
    rt_thread_mdelay((rt_int32_t)num_ms);
    return CY_RSLT_SUCCESS;
}
