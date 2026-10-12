#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Only ONE TLS connection at a time (weather fetch, firmware update check/download).
//
// A TLS session needs ~40-50 KB of contiguous heap (two 16 KB record buffers, the handshake and,
// for the update, the root certificates). This clock only has ~100 KB free, so two sessions at the
// same time starve each other: "SSL - Memory allocation failed", lwip "No more processes" (EAGAIN)
// on send and finally a reset connection. Take a TlsGuard before creating a WiFiClientSecure and
// keep it until the connection is closed; a second task simply waits its turn.
SemaphoreHandle_t tls_lock_handle();

struct TlsGuard
{
    TlsGuard() { xSemaphoreTake(tls_lock_handle(), portMAX_DELAY); }
    ~TlsGuard() { xSemaphoreGive(tls_lock_handle()); }
    TlsGuard(const TlsGuard &) = delete;
    TlsGuard &operator=(const TlsGuard &) = delete;
};