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
// The display keeps its three off-screen sprites (~40 KB) and keeps animating DURING a TLS connection as
// long as the heap is big enough (tls_heap_ok()). Only when it is NOT, the guard asks the display to free
// the sprites (tls_wants_memory()) and waits (up to TLS_FREE_WAIT_MS) for the memory; the display then holds
// the last frame (a short freeze) and creates the sprites again when the last guard is gone (see display.cpp).
SemaphoreHandle_t tls_lock_handle();

// Largest free block and total free heap a TLS handshake needs. Raise TLS_MIN_FREE if the serial log shows
// "SSL - Memory allocation failed"; lower it if the display still freezes although the heap is plentiful.
#ifndef TLS_MIN_BLOCK
#define TLS_MIN_BLOCK 60000
#endif
#ifndef TLS_MIN_FREE
#define TLS_MIN_FREE 100000
#endif

// 0 (default): the display NEVER gives its sprite back for a TLS connection. A 40960-byte sprite that was freed
// can often not be re-created afterwards (the TLS buffers fragment the heap, the biggest free block is then
// ~41 KB), and without the sprite every frame is drawn straight on the panel = permanent flicker.
// 1: old behaviour (free the sprites when the heap is too small, re-create them later).
#ifndef TLS_RELEASE_SPRITES
#define TLS_RELEASE_SPRITES 0
#endif

bool tls_heap_ok();      // enough heap for a TLS connection right now (without freeing anything)
bool tls_wants_memory(); // a TlsGuard is waiting for the display to free its sprites

struct TlsGuard
{
    TlsGuard();
    ~TlsGuard();
    TlsGuard(const TlsGuard &) = delete;
    TlsGuard &operator=(const TlsGuard &) = delete;
};