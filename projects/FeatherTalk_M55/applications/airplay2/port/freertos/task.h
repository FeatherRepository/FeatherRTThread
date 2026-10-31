#pragma once
#include <task.h>
BaseType_t ap_task_create(TaskFunction_t fn,const char *name,uint32_t bytes,void *arg,UBaseType_t priority,TaskHandle_t *handle);
#define xTaskCreate ap_task_create
#define xTaskCreatePinnedToCore(fn,name,bytes,arg,priority,handle,core) ap_task_create(fn,name,bytes,arg,priority,handle)
#define xTaskGetCoreID(task) 0
