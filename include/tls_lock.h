#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Only ONE TLS connection at a time (weather fetch, firmware update check/download).
//
// A TLS session needs ~40-60 KB of contiguous heap (two 16 KB record buffers, the handshake and,
// for the update, the root certificates). This clock only has ~100 KB free, so two sessions at the
// same time starve each other: "SSL - Memory allocation failed", lwip "No more processes" (EAGAIN)
// on send and finally a reset connection. Take a TlsGuard before creating a WiFiClientSecure and
// keep it until the connection is closed; a second task simply waits its turn.
//
// The guard also asks the display to free its three off-screen sprites (40-50 KB) for as long as any
// task holds or waits for it, and waits (up to TLS_FREE_WAIT_MS) until the heap has a block big enough
// for the handshake. The display then draws straight on the screen (a little flicker) and creates the
// sprites again when the last guard is gone (see display.cpp).
SemaphoreHandle_t tls_lock_handle();

struct TlsGuard
{
    TlsGuard();
    ~TlsGuard();
    TlsGuard(const TlsGuard &) = delete;
    TlsGuard &operator=(const TlsGuard &) = delete;
};