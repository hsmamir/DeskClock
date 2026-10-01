#pragma once

// Settings web page on http://<WEB_HOSTNAME>.local/ (mDNS) or the clock's IP.
// Lets you change alarm time, volume, weather city and timezone from a phone;
// values are stored in NVS (see settings.h).
//
// Called repeatedly from the web task (e.g. every 10 ms). It waits until WiFi
// is connected, then starts mDNS + the HTTP server by itself, and restarts
// mDNS if WiFi drops and comes back.
void web_ui_tick();