/*
 * Copyright (c) 2026 FeatherTalk contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal Infineon RTOS abstraction ABI used by btstack-integration.  This
 * header intentionally lives in the application so the upstream SDK remains
 * untouched and the official integration sources can run on RT-Thread.
 */

#ifndef FEATHERTALK_CYABS_RTOS_H
#define FEATHERTALK_CYABS_RTOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <rtthread.h>

#include "cy_result.h"

/* The Infineon HCI integration uses CY_ASSERT without pulling in its usual
 * FreeRTOS platform header.  Map it to RT-Thread's assertion facility. */
#ifndef CY_ASSERT
#define CY_ASSERT(expression) RT_ASSERT(expression)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CY_RTOS_NEVER_TIMEOUT ((uint32_t)0xffffffffUL)

#define CY_RTOS_TIMEOUT       CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_ABSTRACTION_OS, 0)
#define CY_RTOS_NO_MEMORY     CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_ABSTRACTION_OS, 1)
#define CY_RTOS_GENERAL_ERROR CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_ABSTRACTION_OS, 2)
#define CY_RTOS_QUEUE_FULL    CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_ABSTRACTION_OS, 3)
#define CY_RTOS_QUEUE_EMPTY   CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_ABSTRACTION_OS, 4)
#define CY_RTOS_BAD_PARAM     CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_ABSTRACTION_OS, 5)

#define CY_RTOS_MIN_STACK_SIZE 512U
#define CY_RTOS_ALIGNMENT_MASK 0x7U

typedef enum
{
    CY_RTOS_PRIORITY_MIN = 0,
    CY_RTOS_PRIORITY_LOW,
    CY_RTOS_PRIORITY_BELOWNORMAL,
    CY_RTOS_PRIORITY_NORMAL,
    CY_RTOS_PRIORITY_ABOVENORMAL,
    CY_RTOS_PRIORITY_HIGH,
    CY_RTOS_PRIORITY_REALTIME,
    CY_RTOS_PRIORITY_MAX
} cy_thread_priority_t;

typedef enum
{
    CY_TIMER_TYPE_PERIODIC = 0,
    CY_TIMER_TYPE_ONCE
} cy_timer_trigger_type_t;

typedef rt_thread_t cy_thread_t;
typedef void *cy_thread_arg_t;
typedef rt_mutex_t cy_mutex_t;
typedef rt_sem_t cy_semaphore_t;
typedef rt_mq_t cy_queue_t;
typedef rt_timer_t cy_timer_t;
typedef void *cy_timer_callback_arg_t;
typedef uint32_t cy_time_t;
typedef rt_err_t cy_rtos_error_t;

typedef void (*cy_thread_entry_fn_t)(cy_thread_arg_t arg);
typedef void (*cy_timer_callback_t)(cy_timer_callback_arg_t arg);

cy_rtos_error_t cy_rtos_last_error(void);

cy_rslt_t cy_rtos_create_thread(cy_thread_t *thread,
                                cy_thread_entry_fn_t entry_function,
                                const char *name,
                                void *stack,
                                uint32_t stack_size,
                                cy_thread_priority_t priority,
                                cy_thread_arg_t arg);
cy_rslt_t cy_rtos_exit_thread(void);
cy_rslt_t cy_rtos_join_thread(cy_thread_t *thread);
cy_rslt_t cy_rtos_get_thread_handle(cy_thread_t *thread);

cy_rslt_t cy_rtos_init_mutex(cy_mutex_t *mutex);
cy_rslt_t cy_rtos_get_mutex(cy_mutex_t *mutex, cy_time_t timeout_ms);
cy_rslt_t cy_rtos_set_mutex(cy_mutex_t *mutex);
cy_rslt_t cy_rtos_deinit_mutex(cy_mutex_t *mutex);

cy_rslt_t cy_rtos_init_semaphore(cy_semaphore_t *semaphore,
                                 uint32_t maxcount,
                                 uint32_t initcount);
cy_rslt_t cy_rtos_get_semaphore(cy_semaphore_t *semaphore,
                                cy_time_t timeout_ms,
                                bool in_isr);
cy_rslt_t cy_rtos_set_semaphore(cy_semaphore_t *semaphore, bool in_isr);
cy_rslt_t cy_rtos_deinit_semaphore(cy_semaphore_t *semaphore);

cy_rslt_t cy_rtos_init_queue(cy_queue_t *queue, size_t length, size_t itemsize);
cy_rslt_t cy_rtos_put_queue(cy_queue_t *queue,
                            const void *item_ptr,
                            cy_time_t timeout_ms,
                            bool in_isr);
cy_rslt_t cy_rtos_get_queue(cy_queue_t *queue,
                            void *item_ptr,
                            cy_time_t timeout_ms,
                            bool in_isr);
cy_rslt_t cy_rtos_count_queue(cy_queue_t *queue, size_t *num_waiting);
cy_rslt_t cy_rtos_deinit_queue(cy_queue_t *queue);

cy_rslt_t cy_rtos_init_timer(cy_timer_t *timer,
                             cy_timer_trigger_type_t type,
                             cy_timer_callback_t callback,
                             cy_timer_callback_arg_t arg);
cy_rslt_t cy_rtos_start_timer(cy_timer_t *timer, cy_time_t num_ms);
cy_rslt_t cy_rtos_stop_timer(cy_timer_t *timer);
cy_rslt_t cy_rtos_is_running_timer(cy_timer_t *timer, bool *state);
cy_rslt_t cy_rtos_deinit_timer(cy_timer_t *timer);

cy_rslt_t cy_rtos_get_time(cy_time_t *tval);
cy_rslt_t cy_rtos_delay_milliseconds(cy_time_t num_ms);

#ifdef __cplusplus
}
#endif

#endif
