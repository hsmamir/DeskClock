#include "tls_lock.h"
#include "state.h"

// How long the guard waits for the display to hand its sprites back, and the largest free block a TLS
// handshake with certificate checking needs (the same 60000 the update install waits for).
#ifndef TLS_FREE_WAIT_MS
#define TLS_FREE_WAIT_MS 3000
#endif

static volatile bool s_wantsMemory = false;

bool tls_heap_ok()
{
    return ESP.getMaxAllocHeap() >= TLS_MIN_BLOCK && ESP.getFreeHeap() >= TLS_MIN_FREE;
}

bool tls_wants_memory()
{
    return s_wantsMemory;
}

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

    // Enough heap already (the normal case): go on at once, the display keeps its sprites and keeps animating.
    // Otherwise ask the display to free them; it sees the request within one frame (~33 ms).
    int waited = 0;
    const bool ok = tls_heap_ok();
    if (!ok)
    {
        s_wantsMemory = true;
        while (waited < TLS_FREE_WAIT_MS && !tls_heap_ok())
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            waited += 50;
        }
    }
    Serial.printf("TLS: heap free %u, largest block %u (%s, waited %d ms)\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap(), ok ? "display keeps running" : "display freed its sprites", waited);
}

TlsGuard::~TlsGuard()
{
    s_wantsMemory = false; // before giving the lock back, so a waiting guard's own request is not wiped
    xSemaphoreGive(tls_lock_handle());
    tlsUsersAdd(-1);
}