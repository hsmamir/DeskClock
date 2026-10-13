#include "tls_lock.h"
#include "state.h"

// How long the guard waits for the display to hand its sprites back, and the largest free block a TLS
// handshake with certificate checking needs (the same 60000 the update install waits for).
#ifndef TLS_FREE_WAIT_MS
#define TLS_FREE_WAIT_MS 3000
#endif
#ifndef TLS_MIN_BLOCK
#define TLS_MIN_BLOCK 60000
#endif

// The function-local static is created thread-safely on first use, so it does not matter which
// task asks first and no init call is needed in setup().
SemaphoreHandle_t tls_lock_handle()
{
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}

static void tlsUsersAdd(int delta)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_state.tlsUsers = (uint8_t)(g_state.tlsUsers + delta);
    xSemaphoreGive(g_stateMutex);
}

TlsGuard::TlsGuard()
{
    // Counted BEFORE waiting for the lock and uncounted AFTER giving it back, so the count never drops
    // to 0 between two TLS users that follow each other (the display would restore and free again).
    tlsUsersAdd(+1);
    xSemaphoreTake(tls_lock_handle(), portMAX_DELAY);

    // The display task sees the count within one frame (~33 ms) and frees its sprites.
    int waited = 0;
    while (waited < TLS_FREE_WAIT_MS && ESP.getMaxAllocHeap() < TLS_MIN_BLOCK)
    {
        vTaskDelay(pdMS_TO_TICKS(50));
        waited += 50;
    }
    Serial.printf("TLS: heap free %u, largest block %u (waited %d ms for memory)\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap(), waited);
}

TlsGuard::~TlsGuard()
{
    xSemaphoreGive(tls_lock_handle());
    tlsUsersAdd(-1);
}