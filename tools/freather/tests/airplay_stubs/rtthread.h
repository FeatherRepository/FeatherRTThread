#ifndef AP_TEST_RTTHREAD_H
#define AP_TEST_RTTHREAD_H
#include <stddef.h>
#define RT_NULL NULL
#define RT_EOK 0
#define RT_ERROR 1
#define RT_EIO 5
#define RT_EBUSY 16
#define RT_EINVAL 22
#define RT_ENOSYS 38
#define RT_IPC_FLAG_PRIO 0
#define RT_WAITING_FOREVER -1
#define INIT_APP_EXPORT(fn) int ap_test_init(void) { return fn(); }
struct rt_mutex { int locked; };
int rt_mutex_init(struct rt_mutex *, const char *, int);
int rt_mutex_take(struct rt_mutex *, int);
int rt_mutex_release(struct rt_mutex *);
#endif
