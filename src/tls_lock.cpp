#include "tls_lock.h"

// The function-local static is created thread-safely on first use, so it does not matter which
// task asks first and no init call is needed in setup().
SemaphoreHandle_t tls_lock_handle()
{
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}