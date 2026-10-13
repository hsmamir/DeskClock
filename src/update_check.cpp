#include <Arduino.h>
#include "update_check.h"
#include "config.h"
#include "version.h"

#if defined(USE_WOKWI_WIFI) || !UPDATE_ENABLED

// The simulator (or a build with UPDATE_ENABLED 0) has no update feature.
void update_tick() {}
UpdateStatus update_status() { return UpdateStatus(); }
bool update_request_check() { return false; }
bool update_request_install(const String &, String &err)
{
    err = "disabled";
    return false;
}

#else

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <ctype.h>
#include <string.h>
#include <esp_ota_ops.h>
#include <mbedtls/md.h>
#include "state.h"
#include "time_sync.h"
#include "alarm_ctrl.h"
#include "update_certs.h"
#include "tls_lock.h"

// Created on first use (thread-safe function-local static, same pattern as tls_lock.cpp), so it does
// not matter which task asks first and no init call is needed in setup().
static SemaphoreHandle_t updateMutex()
{
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}
static UpdateStatus s_st;
static String s_binUrl, s_shaUrl;                          // found by the last check
static bool s_running = false;                             // a worker task exists
static UpdateStatus s_prev;                                // status before an automatic check; restored when that check fails
static uint32_t s_autoNextMs = UPDATE_AUTO_FIRST_DELAY_MS; // millis() when the next automatic check is due

namespace
{
    struct Lock
    {
        Lock() { xSemaphoreTake(updateMutex(), portMAX_DELAY); }
        ~Lock() { xSemaphoreGive(updateMutex()); }
    };
}

// The worker does TLS with full certificate verification (X.509 chain check, RSA-4096 / ECDSA P-384),
// which needs a lot more stack and CPU time than the unverified weather request. The stack was 10240.
// The priority is the IDLE priority on purpose: at priority 1 this CPU-bound task never lets the
// core-0 idle task run during the handshake, and the task watchdog (which watches IDLE0 and panics)
// would reset the chip. At priority 0 it time-slices with the idle task.
#ifndef UPDATE_TASK_STACK
#define UPDATE_TASK_STACK 12288
#endif

static const char *const ASSET_BIN = "deskclock-" FW_VARIANT ".bin";
static const char *const ASSET_SHA = "deskclock-" FW_VARIANT ".bin.sha256";

// ---------------------------------------------------------------- small helpers

static void setOtaPercent(int pct)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_state.otaPercent = (int8_t)pct;
    xSemaphoreGive(g_stateMutex);
}

static void setFailed(const String &code)
{
    Serial.printf("Update: failed (%s)\n", code.c_str());
    setOtaPercent(-1);
    Lock l;
    s_st.state = UpdateState::FAILED;
    s_st.error = code;
}

// "v1.2.3" / "1.2" / "1.2.3-beta" -> {1,2,3}. False if there is no number at all ("dev").
static bool parseVersion(const char *s, int v[3])
{
    v[0] = v[1] = v[2] = 0;
    if (!s)
        return false;
    if (*s == 'v' || *s == 'V')
        s++;
    int n = sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]);
    return n >= 1;
}

static int compareVersions(const int a[3], const int b[3])
{
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

static String hostOf(const String &url)
{
    int hostEnd = url.indexOf('/', 8);
    String host = url.substring(8, hostEnd < 0 ? url.length() : hostEnd);
    host.toLowerCase();
    return host;
}

// Every TLS connection parses all root certificates it is given into RAM, and this clock has little
// free heap ("SSL - Memory allocation failed"). So each connection only gets the roots of its own host.
static const char *rootsFor(const String &url)
{
    return hostOf(url).endsWith(".githubusercontent.com") ? UPDATE_ROOT_CAS_CDN : UPDATE_ROOT_CAS_API;
}

// Only GitHub's own hosts are accepted, over https (also after a redirect).
static bool allowedUrl(const String &url)
{
    if (!url.startsWith("https://"))
        return false;
    int hostEnd = url.indexOf('/', 8);
    String host = url.substring(8, hostEnd < 0 ? url.length() : hostEnd);
    host.toLowerCase();
    return host == "github.com" || host == "api.github.com" ||
           host.endsWith(".github.com") || host.endsWith(".githubusercontent.com");
}

static bool preconditions(String &err)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        err = "wifi";
        return false;
    }
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool timeOk = g_state.timeSynced && !g_state.timeApprox; // certificates have dates: needs real NTP time
    xSemaphoreGive(g_stateMutex);
    if (!timeOk)
    {
        err = "time";
        return false;
    }
    if (ESP.getFreeHeap() < UPDATE_MIN_FREE_HEAP || ESP.getMaxAllocHeap() < 32768)
    {
        err = "heap";
        return false;
    }
    return true;
}

static String httpError(int code, WiFiClientSecure &client)
{
    if (code < 0)
    {
        char buf[100] = {0};
        int tlsErr = client.lastError(buf, sizeof(buf));
        // The real reason goes to Serial: e.g. -0x2700 = certificate verify failed,
        // -0x7F00 = out of memory, -0x0010 = connection reset.
        Serial.printf("Update: request failed, http %d, tls error %d (%s), heap free %u, largest block %u\n",
                      code, tlsErr, buf, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
        return tlsErr != 0 ? "tls" : "connect";
    }
    if (code == 403 || code == 429)
        return "rate_limit";
    return "http_" + String(code);
}

// GET `url`, following redirects by hand (a release download goes from github.com to another
// host). On success the response is open, status 200, ready to be read; call http.end() after.
static bool openUrl(WiFiClientSecure &client, HTTPClient &http, String url, bool http10, String &err)
{
    static const char *keys[] = {"Location", "Transfer-Encoding"};
    for (int hop = 0; hop < 4; hop++)
    {
        if (!allowedUrl(url))
        {
            // only the host is logged: the rest of a download URL carries a temporary signature
            Serial.printf("Update: refusing a redirect to %s (host not on the GitHub allow-list)\n",
                          url.startsWith("https://") ? hostOf(url).c_str() : "a non-https address");
            err = "redirect";
            return false;
        }
        // The TLS handshake needs a burst of memory (record buffers, big-number maths for the
        // certificate check). If another task (display, web page) holds it at that very moment the
        // handshake fails with "Memory allocation failed". That is temporary: wait and try again.
        int code = -1;
        for (int attempt = 0; attempt < 4; attempt++)
        {
            if (!http.begin(client, url))
            {
                err = "connect";
                return false;
            }
            client.setCACert(rootsFor(url)); // the connection itself is opened by GET()
            http.setReuse(false);
            http.setTimeout(12000);
            http.setUserAgent("DeskClock/" FW_VERSION);
            http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
            http.useHTTP10(http10); // HTTP/1.0 = no chunked encoding, so the JSON can be streamed
            http.collectHeaders(keys, 2);
            if (http10)
                http.addHeader("Accept", "application/vnd.github+json"); // the API call (the downloads need no header)

            code = http.GET();
            if (code >= 0 || attempt == 3)
                break;
            char msg[100] = {0};
            int tlsErr = client.lastError(msg, sizeof(msg));
            for (char *c = msg; *c; c++)
                *c = tolower((unsigned char)*c); // "Memory allocation failed" / "Allocation of memory failed"
            if (tlsErr == 0 || strstr(msg, "allocation") == nullptr)
                break; // some other connection problem: not worth repeating
            Serial.printf("Update: out of memory during the TLS handshake (%s), heap free %u, largest block %u; retry %d\n",
                          msg, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), attempt + 1);
            http.end();
            client.stop();
            vTaskDelay(pdMS_TO_TICKS(2500));
        }
        Serial.printf("Update: %s -> HTTP %d\n", hostOf(url).c_str(), code);
        if (code == 200)
            return true;
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308)
        {
            String loc = http.header("Location");
            String from = hostOf(url);
            http.end();
            client.stop();
            if (loc.length() == 0)
            {
                Serial.println("Update: redirect without a Location header");
                err = "redirect";
                return false;
            }
            if (loc.startsWith("/")) // relative redirect: same host
                loc = "https://" + from + loc;
            Serial.printf("Update: redirect to %s (%u characters)\n",
                          loc.startsWith("https://") ? hostOf(loc).c_str() : "a non-https address", (unsigned)loc.length());
            url = loc;
            continue;
        }
        err = httpError(code, client);
        http.end();
        return false;
    }
    err = "redirect";
    return false;
}

// Reads an HTTP response body from the TLS socket for the JSON parser. Reading the socket
// byte by byte straight from the parser ended the JSON early ("IncompleteInput") whenever the next TLS
// record had not arrived yet. This wrapper waits for data (until `timeoutMs` without any), only reports
// the end when the server really closed the connection, and decodes chunked transfer encoding
// (the response has no Content-Length, so it may be chunked).
class BodyStream : public Stream
{
public:
    BodyStream(WiFiClient &c, bool chunked, uint32_t timeoutMs) : c_(c), chunked_(chunked), timeoutMs_(timeoutMs)
    {
        setTimeout(0); // Stream::timedRead() must not wait again on top of our own waiting
    }

    int read() override
    {
        if (!chunked_)
            return rawRead();
        if (chunkLeft_ == 0)
        {
            if (done_ || !readChunkSize() || chunkLeft_ == 0)
            {
                done_ = true;
                return -1;
            }
        }
        int b = rawRead();
        if (b < 0)
            return -1;
        if (--chunkLeft_ == 0)
        {
            rawRead(); // CR
            rawRead(); // LF
        }
        return b;
    }
    int available() override { return 0; }
    int peek() override { return -1; }
    size_t write(uint8_t) override { return 0; }
    void flush() override {}

    size_t count() const { return count_; }
    const char *endReason() const { return end_; }

private:
    int rawRead()
    {
        uint32_t t0 = millis();
        for (;;)
        {
            if (c_.available() > 0)
            {
                int b = c_.read();
                if (b >= 0)
                {
                    count_++;
                    return b;
                }
            }
            if (!c_.connected() && c_.available() <= 0)
            {
                end_ = "closed";
                return -1;
            }
            if (millis() - t0 > timeoutMs_)
            {
                end_ = "timeout";
                return -1;
            }
            vTaskDelay(1);
        }
    }

    // "1f4a\r\n" (a ";extension" may follow the number) -> chunkLeft_
    bool readChunkSize()
    {
        uint32_t n = 0;
        bool any = false, ext = false;
        for (;;)
        {
            int b = rawRead();
            if (b < 0)
                return false;
            if (b == '\n')
                break;
            if (b == '\r' || ext)
                continue;
            if (b == ';')
            {
                ext = true;
                continue;
            }
            if (!isxdigit(b))
                return false;
            n = n * 16 + (b <= '9' ? b - '0' : (b | 0x20) - 'a' + 10);
            any = true;
        }
        if (!any) // an empty line (the CRLF after the previous chunk): try again
            return readChunkSize();
        chunkLeft_ = n;
        return true;
    }

    WiFiClient &c_;
    bool chunked_;
    uint32_t timeoutMs_;
    uint32_t chunkLeft_ = 0;
    bool done_ = false;
    size_t count_ = 0;
    const char *end_ = "";
};

// ---------------------------------------------------------------- 1. check

static void doCheck()
{
    TlsGuard tlsGuard; // waits for a running weather fetch; one TLS connection at a time (tls_lock.h)
    String err;
    if (!preconditions(err))
    {
        setFailed(err);
        return;
    }

    WiFiClientSecure client;
    HTTPClient http;
    if (!openUrl(client, http, "https://api.github.com/repos/" UPDATE_REPO "/releases/latest", true, err))
    {
        setFailed(err == "http_404" ? String("no_release") : err);
        return;
    }

    // content-length -1 = unknown (e.g. chunked, which the JSON stream parser below cannot read)
    Serial.printf("Update: connected, content-length %d, heap free %u, largest block %u, lowest ever %u\n",
                  http.getSize(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)ESP.getMinFreeHeap());

    // Keep only what is needed: the response is large (release notes, uploader details, ...).
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["html_url"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["size"] = true;
    filter["assets"][0]["browser_download_url"] = true;

    String te = http.header("Transfer-Encoding");
    te.toLowerCase();
    BodyStream body(http.getStream(), te.indexOf("chunked") >= 0, 15000);

    JsonDocument doc;
    DeserializationError je = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    http.end();
    client.stop();
    if (je)
    {
        // NoMemory = heap ran out; IncompleteInput = the body ended early (see "ended" below);
        // InvalidInput = not JSON.
        Serial.printf("Update: JSON error %s after %u bytes (ended: %s, chunked: %s), heap free %u, largest block %u\n",
                      je.c_str(), (unsigned)body.count(), body.endReason()[0] ? body.endReason() : "parser", te.c_str(),
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
        // The parser keeps what it read before the break. Everything needed is checked below
        // (tag, both file URLs, size), so a cut-off tail of the release notes does no harm.
        if (!(je == DeserializationError::IncompleteInput && (doc["tag_name"] | "")[0]))
        {
            setFailed("json");
            return;
        }
        Serial.println("Update: partial JSON accepted, the needed fields are complete");
    }

    String tag = doc["tag_name"] | "";
    String notes = doc["html_url"] | "";
    String binUrl, shaUrl;
    uint32_t size = 0;
    for (JsonObject a : doc["assets"].as<JsonArray>())
    {
        String name = a["name"] | "";
        if (name == ASSET_BIN)
        {
            binUrl = a["browser_download_url"] | "";
            size = a["size"] | 0;
        }
        else if (name == ASSET_SHA)
            shaUrl = a["browser_download_url"] | "";
    }

    int latest[3], current[3];
    if (tag.length() == 0 || !parseVersion(tag.c_str(), latest))
    {
        setFailed("bad_tag");
        return;
    }
    if (binUrl.length() == 0 || shaUrl.length() == 0 || size == 0 || !allowedUrl(binUrl) || !allowedUrl(shaUrl))
    {
        setFailed("no_asset"); // a release exists, but not for this display variant
        return;
    }
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    if (!next || size > next->size)
    {
        setFailed("too_big");
        return;
    }

    // A "dev" build (no version number) is offered the release; otherwise only a newer one.
    bool haveCurrent = parseVersion(FW_VERSION, current);
    bool newer = !haveCurrent || compareVersions(latest, current) > 0;

    Lock l;
    s_st.latest = tag;
    s_st.notesUrl = notes;
    s_st.sizeBytes = size;
    s_st.error = "";
    s_st.percent = 0;
    s_st.checkedMs = millis() ? millis() : 1; // 0 means "never"
    s_autoNextMs = millis() + UPDATE_AUTO_CHECK_INTERVAL_MS;
    s_binUrl = binUrl;
    s_shaUrl = shaUrl;
    s_st.state = newer ? UpdateState::AVAILABLE : UpdateState::UP_TO_DATE;
    Serial.printf("Update: latest %s, running %s -> %s\n", tag.c_str(), FW_VERSION, newer ? "available" : "up to date");
}

// ---------------------------------------------------------------- 2. install (after the user confirmed)

// Receives the download: writes it to the other OTA slot and hashes it on the way.
class UpdateSink : public Stream
{
public:
    explicit UpdateSink(size_t total) : total_(total)
    {
        mbedtls_md_init(&ctx_);
        ok_ = mbedtls_md_setup(&ctx_, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0) == 0 &&
              mbedtls_md_starts(&ctx_) == 0;
    }
    ~UpdateSink() { mbedtls_md_free(&ctx_); }

    size_t write(const uint8_t *buf, size_t len) override
    {
        if (!ok_ || len == 0)
            return 0;
        if (Update.write(const_cast<uint8_t *>(buf), len) != len || mbedtls_md_update(&ctx_, buf, len) != 0)
        {
            ok_ = false;
            return 0;
        }
        done_ += len;
        int pct = total_ ? (int)((uint64_t)done_ * 100 / total_) : 0;
        if (pct != lastPct_)
        {
            lastPct_ = pct;
            setOtaPercent(pct);
            Lock l;
            s_st.percent = (uint8_t)pct;
        }
        return len;
    }
    size_t write(uint8_t b) override { return write(&b, 1); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

    bool ok() const { return ok_; }
    size_t done() const { return done_; }
    bool finish(uint8_t out[32]) { return ok_ && mbedtls_md_finish(&ctx_, out) == 0; }

private:
    mbedtls_md_context_t ctx_;
    size_t total_;
    size_t done_ = 0;
    int lastPct_ = -1;
    bool ok_ = false;
};

static void toHex(const uint8_t *in, size_t n, char *out)
{
    static const char *d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++)
    {
        out[2 * i] = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 15];
    }
    out[2 * n] = 0;
}

static void doInstall()
{
    TlsGuard tlsGuard; // held for the whole install: weather waits instead of competing for heap
    String binUrl, shaUrl;
    uint32_t size;
    {
        Lock l;
        binUrl = s_binUrl;
        shaUrl = s_shaUrl;
        size = s_st.sizeBytes;
    }

    String err;
    if (!preconditions(err))
    {
        setFailed(err);
        return;
    }
    setOtaPercent(0);

    // The display task frees its off-screen sprites now that only the progress screen is shown (see
    // display.cpp). Give it a moment: the TLS handshake needs that memory at once.
    for (int waited = 0; waited < 3000 && ESP.getMaxAllocHeap() < 60000; waited += 100)
        vTaskDelay(pdMS_TO_TICKS(100));
    Serial.printf("Update: memory for the download: heap free %u, largest block %u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());

    // 1) the published SHA-256 of the file (64 hex characters, as written by `sha256sum`)
    String expected;
    {
        WiFiClientSecure client;
        HTTPClient http;
        if (!openUrl(client, http, shaUrl, false, err))
        {
            setFailed(err);
            return;
        }
        expected = http.getString();
        http.end();
        client.stop();
    }
    expected.trim();
    expected = expected.substring(0, 64);
    expected.toLowerCase();
    bool hexOk = expected.length() == 64;
    for (size_t i = 0; hexOk && i < 64; i++)
        hexOk = isxdigit((unsigned char)expected[i]);
    if (!hexOk)
    {
        setFailed("bad_checksum_file");
        return;
    }

    // 2) the firmware itself
    WiFiClientSecure client;
    HTTPClient http;
    if (!openUrl(client, http, binUrl, false, err))
    {
        setFailed(err);
        return;
    }
    int len = http.getSize();
    if (len > 0 && (uint32_t)len != size)
    {
        http.end();
        setFailed("size_mismatch");
        return;
    }
    if (!Update.begin(size, U_FLASH))
    {
        http.end();
        setFailed("flash_begin");
        return;
    }

    UpdateSink sink(size);
    http.writeToStream(&sink);
    http.end();
    client.stop();

    uint8_t digest[32];
    char hex[65];
    if (!sink.ok() || sink.done() != size || !sink.finish(digest))
    {
        Update.abort();
        setFailed("download");
        return;
    }
    toHex(digest, 32, hex);
    if (expected != hex)
    {
        Update.abort(); // never activate a file that does not match its published checksum
        setFailed("checksum");
        return;
    }
    if (!Update.end(false)) // verifies the image and makes the new slot the boot slot
    {
        setFailed("flash_end");
        return;
    }

    {
        Lock l;
        s_st.state = UpdateState::DONE;
        s_st.percent = 100;
    }
    setOtaPercent(100);
    Serial.println("Update: written and verified, rebooting");
    time_save_now();                 // keep the clock accurate across the reboot, like the espota path
    vTaskDelay(pdMS_TO_TICKS(1500)); // let the settings page see "done"
    ESP.restart();
}

// ---------------------------------------------------------------- public API

static void worker(void *arg)
{
    int mode = (int)(intptr_t)arg; // 0 manual check, 1 install, 2 automatic check
    Serial.printf("Update: worker start (mode %d), heap free %u, largest block %u\n", mode,
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    if (mode == 1)
        doInstall();
    else
    {
        doCheck();
        if (mode == 2)
        {
            // Nobody asked for this check, so a failure must not replace what the page showed
            // before (e.g. "version X is ready"). Try again sooner than the normal interval.
            Lock l;
            if (s_st.state == UpdateState::FAILED)
            {
                Serial.printf("Update: automatic check failed (%s), retrying later\n", s_st.error.c_str());
                s_st = s_prev;
                s_autoNextMs = millis() + UPDATE_AUTO_RETRY_MS;
            }
        }
    }
    Serial.printf("Update: worker end, stack never used: %u of %u bytes, heap free %u\n",
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned)UPDATE_TASK_STACK,
                  (unsigned)ESP.getFreeHeap());
    {
        Lock l;
        s_running = false;
    }
    vTaskDelete(nullptr);
}

// Caller holds no lock. Marks the status first so the page reacts immediately.
static bool startWorker(int mode, String &err)
{
    if (xTaskCreatePinnedToCore(worker, "update", UPDATE_TASK_STACK, (void *)(intptr_t)mode, tskIDLE_PRIORITY, nullptr, 0) != pdPASS)
    {
        Lock l;
        s_running = false;
        s_st.state = UpdateState::FAILED;
        s_st.error = err = "heap";
        return false;
    }
    return true;
}

void update_tick()
{
#if UPDATE_AUTO_CHECK
    uint32_t now = millis();
    {
        Lock l;
        if ((int32_t)(now - s_autoNextMs) < 0)
            return; // not due yet
        if (s_running || s_st.state == UpdateState::DOWNLOADING || s_st.state == UpdateState::DONE)
        {
            s_autoNextMs = now + 60000; // a check or install is already running
            return;
        }
    }
    String err;
    if (alarm_get_state() != AlarmState::IDLE || !preconditions(err))
    {
        // No WiFi / real time / memory yet, or the alarm is busy: ask again in a minute, silently.
        Lock l;
        s_autoNextMs = now + 60000;
        return;
    }
    {
        Lock l;
        if (s_running)
            return;
        s_running = true;
        s_prev = s_st;
        s_st.state = UpdateState::CHECKING;
        s_st.error = "";
        s_autoNextMs = now + UPDATE_AUTO_RETRY_MS; // overwritten by the result
    }
    if (!startWorker(2, err))
    {
        Lock l;
        s_st = s_prev; // startWorker marked it FAILED; an automatic check stays silent
    }
#endif
}

UpdateStatus update_status()
{
    Lock l;
    return s_st;
}

bool update_request_check()
{
    {
        Lock l;
        if (s_running)
            return false;
        s_running = true;
        s_st.state = UpdateState::CHECKING;
        s_st.error = "";
    }
    String err;
    return startWorker(0, err);
}

bool update_request_install(const String &version, String &err)
{
    if (alarm_get_state() != AlarmState::IDLE)
    {
        err = "alarm_busy"; // the alarm cannot ring during the update
        return false;
    }
    {
        Lock l;
        if (s_running)
        {
            err = "busy";
            return false;
        }
        if (s_st.state != UpdateState::AVAILABLE || version != s_st.latest)
        {
            err = "not_available";
            return false;
        }
        s_running = true;
        s_st.state = UpdateState::DOWNLOADING;
        s_st.percent = 0;
        s_st.error = "";
    }
    return startWorker(1, err);
}

#endif