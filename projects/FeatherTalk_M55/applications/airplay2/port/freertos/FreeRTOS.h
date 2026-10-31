#pragma once
#include <FreeRTOS.h>
#include <rtthread.h>
#define portMUX_TYPE rt_base_t
#define portMUX_INITIALIZER_UNLOCKED 0
#undef portENTER_CRITICAL
#undef portEXIT_CRITICAL
#define portENTER_CRITICAL(m) (*(m)=rt_hw_interrupt_disable())
#define portEXIT_CRITICAL(m) rt_hw_interrupt_enable(*(m))
#define portNUM_PROCESSORS 1
