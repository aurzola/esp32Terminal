#include "ssh.h"
#include "passprompt.h"

#include <string.h>

#include <Arduino.h>
#include <WiFi.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"

#include "libssh_esp32.h"
#include <libssh/libssh.h>

SshSession ssh;

// Host -> terminal bytes flow through a byte stream buffer (the SSH task
// sends, the main loop receives); keyboard bytes and the password go the
// other way through queues. Key sequences from KeyMap are at most 5 bytes.
struct KeyMsg {
    uint8_t len;
    uint8_t data[8];
};

namespace {

// Task storage for the SSH task. libssh needs a large stack (crypto), and the
// task touches the flash-backed WiFi stack (connect reads calibration/NVS with
// the cache disabled), so the stack MUST live in internal DRAM: a PSRAM stack
// aborts in spi_flash_disable_interrupts_caches_and_other_cpu() because the
// task's own stack is unreachable while the cache is off. Internal DRAM is
// what this core has left after the CRT video framebuffer (which is in PSRAM).
StaticTask_t s_taskBuf;
StackType_t *s_stack = nullptr;

} // namespace

// Status bar clock (ticket 10): start NTP once WiFi is up. configTzTime()
// drives the sntp background sync and applies the POSIX TZ string (TERM_TZ)
// so localtime_r() renders local time with DST. The main loop treats the time
// as valid once time() is past a boot-epoch threshold (the sntp sync-status
// enum is transient and unreliable).
static void ntpSync(void)
{
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    configTzTime(TERM_TZ, "pool.ntp.org");
    Serial.printf("[ssh] ntp sync started (tz %s)\n", TERM_TZ);
}

void SshSession::begin(void)
{
    state_ = SSH_STATE_WIFI;
    attempt_ = 0;
    prompt_.init();
    rx_ = xStreamBufferCreate(4096, 1);
    keys_ = xQueueCreate(32, sizeof(KeyMsg));
    pw_ = xQueueCreate(2, TERM_PASSWD_MAX);
    if (rx_ == nullptr || keys_ == nullptr || pw_ == nullptr) {
        Serial.printf("[ssh] ERROR: no heap for session queues\n");
        return;
    }
    Serial.printf("[ssh] internal heap free=%u largest=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(
                      MALLOC_CAP_INTERNAL));
    s_stack = (StackType_t *)heap_caps_malloc(
        TERM_SSH_STACK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_stack == nullptr) {
        Serial.printf("[ssh] ERROR: no internal RAM for task stack\n");
        return;
    }
    Serial.printf("[ssh] internal heap free after stack=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    libssh_begin();
    xTaskCreateStaticPinnedToCore(taskMain, "sshTask",
                                  TERM_SSH_STACK_BYTES / sizeof(StackType_t),
                                  this, 3, s_stack, &s_taskBuf, 0);
    Serial.printf("[ssh] task up, target %s@%s:%d\n", TERM_SSH_USER,
                  TERM_SSH_HOST, TERM_SSH_PORT);
}

void SshSession::taskMain(void *arg)
{
    static_cast<SshSession *>(arg)->run();
    vTaskDelete(nullptr);
}

bool SshSession::connectWifi(void)
{
    // begin() on a cooldown: re-issuing it while "sta is connecting" is refused,
    // and re-issuing it while already connected would tear down the TCP/SSH
    // stack. Negative start allows the first attempt right after boot.
    static unsigned long lastBeginMs = (uint32_t)-6000;
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        return true;
    }
    if (millis() - lastBeginMs > 5000) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(TERM_WIFI_SSID, TERM_WIFI_PASS);
        lastBeginMs = millis();
    }
    unsigned long t0 = millis();
    wl_status_t lastLogged = (wl_status_t)0xff;
    while (true) {
        st = WiFi.status();
        if (st != lastLogged) {
            lastLogged = st;
            Serial.printf("[ssh] wifi status=%d\n", (int)st);
        }
        if (st == WL_CONNECTED) {
            Serial.printf("[ssh] wifi connected ip=%s gw=%s\n",
                          WiFi.localIP().toString().c_str(),
                          WiFi.gatewayIP().toString().c_str());
            return true;
        }
        if (millis() - t0 > TERM_SSH_WIFI_TIMEOUT_MS) {
            Serial.printf("[ssh] wifi timeout, status=%d\n", (int)st);
            return false; // still connecting/associating: retry waits on it
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Block until the main loop submits the password (or the connection drops).
bool SshSession::waitPassword(char *pw, void *vsession)
{
    ssh_session session = (ssh_session)vsession;
    for (;;) {
        if (xQueueReceive((QueueHandle_t)pw_, pw, 0) == pdTRUE) {
            return true;
        }
        if (session == nullptr || !ssh_is_connected(session)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void SshSession::pumpChannel(void *vsession, void *vchannel)
{
    ssh_session session = (ssh_session)vsession;
    ssh_channel channel = (ssh_channel)vchannel;
    uint8_t buf[256];
    KeyMsg km;
    while (ssh_channel_is_open(channel) && !ssh_channel_is_eof(channel)) {
        // Drain pending keystrokes into the channel.
        while (xQueueReceive((QueueHandle_t)keys_, &km, 0) == pdTRUE) {
            if (ssh_channel_write(channel, km.data, km.len) < 0) {
                break;
            }
        }
        // Non-blocking host read (50ms): returns bytes or SSH_AGAIN.
        int n = ssh_channel_read_timeout(channel, buf, sizeof(buf), 0, 50);
        if (n > 0) {
            xStreamBufferSend((StreamBufferHandle_t)rx_, buf, (size_t)n, 0);
        } else if (n == SSH_ERROR) {
            break;
        }
        if (WiFi.status() != WL_CONNECTED) {
            break;
        }
    }
}

void SshSession::retryDelay(void)
{
    uint32_t ms = sshBackoffDelayMs(attempt_++);
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void SshSession::run(void)
{
    // Log the real stack usage once after boot so TERM_SSH_STACK_BYTES can be
    // tuned to the smallest size that fits internal DRAM.
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("[ssh] task stack hwm=%u/%u\n", (unsigned)hwm,
                  (unsigned)(TERM_SSH_STACK_BYTES / sizeof(StackType_t)));
    for (;;) {
        state_ = SSH_STATE_WIFI;
        if (!connectWifi()) {
            state_ = SSH_STATE_RETRY;
            retryDelay();
            continue;
        }
        ntpSync();

        state_ = SSH_STATE_CONNECT;
        ssh_session session = ssh_new();
        if (session != nullptr) {
            int verbosity = TERM_SSH_VERBOSITY;
            int port = TERM_SSH_PORT;
            int noConfig = 0;
            ssh_options_set(session, SSH_OPTIONS_HOST, TERM_SSH_HOST);
            ssh_options_set(session, SSH_OPTIONS_USER, TERM_SSH_USER);
            ssh_options_set(session, SSH_OPTIONS_PORT, &port);
            ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
            long sshTimeoutSec = TERM_SSH_TIMEOUT_SEC;
            ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &sshTimeoutSec);
            // Skip the ssh_config file parsing on a fixed personal device.
            ssh_options_set(session, SSH_OPTIONS_PROCESS_CONFIG, &noConfig);

            if (ssh_connect(session) == SSH_OK) {
                // Local password prompt: the main loop collects it masked on
                // the CRT (PassPrompt) and submits it once Enter is pressed.
                // A wrong password re-prompts on the same connection; a drop
                // while waiting falls through to the reconnect/backoff below.
                // When TERM_SSH_PWD is set (auto-login), the first attempt
                // uses it directly and only falls back to the CRT prompt if
                // the host rejects it.
                char pw[TERM_PASSWD_MAX];
                bool autoLogin = TERM_SSH_PWD[0] != '\0';
                int rc = SSH_AUTH_DENIED;
                bool first = true;
                while (rc == SSH_AUTH_DENIED) {
                    if (first && autoLogin) {
                        strncpy(pw, TERM_SSH_PWD, sizeof(pw) - 1);
                        pw[sizeof(pw) - 1] = '\0';
                        first = false;
                    } else {
                        state_ = SSH_STATE_AUTH;
                        // Purge any stale queued password so the previous
                        // connection's secret cannot auto-authenticate this one.
                        xQueueReset((QueueHandle_t)pw_);
                        if (!waitPassword(pw, session)) {
                            rc = SSH_AUTH_ERROR; // dropped while waiting
                            break;
                        }
                        first = false;
                    }
                    state_ = SSH_STATE_AUTHING;
                    rc = ssh_userauth_password(session, NULL, pw);
                    memset(pw, 0, sizeof(pw));
                    if (rc != SSH_AUTH_SUCCESS) {
                        Serial.printf("[ssh] auth rc=%d\n", rc);
                    }
                }
                if (rc == SSH_AUTH_SUCCESS) {
                    ssh_channel channel = ssh_channel_new(session);
                    if (channel != nullptr &&
                        ssh_channel_open_session(channel) == SSH_OK) {
                        // The grid's last row is the fixed status line,
                        // so the session owns rows 0..ROWS-2.
                        int ptyRc = ssh_channel_request_pty_size(
                            channel, "xterm", TERM_COLS, TERM_ROWS - 1);
                        int shellRc = SSH_ERROR;
                        if (ptyRc == SSH_OK) {
                            shellRc = ssh_channel_request_shell(channel);
                        }
                        if (ptyRc == SSH_OK && shellRc == SSH_OK) {
                            state_ = SSH_STATE_SHELL;
                            attempt_ = 0;
                            pumpChannel(session, channel);
                        } else {
                            Serial.printf("[ssh] pty/shell request failed "
                                          "(pty=%d shell=%d), reconnecting\n",
                                          ptyRc, shellRc);
                        }
                        ssh_channel_free(channel);
                    }
                }
            } else {
                Serial.printf("[ssh] connect failed: %s\n",
                              ssh_get_error(session));
            }
            ssh_disconnect(session);
            ssh_free(session);
        }

        state_ = SSH_STATE_RETRY;
        retryDelay();
    }
}

void SshSession::sendKey(const uint8_t *seq, size_t n)
{
    if (n == 0 || keys_ == nullptr) {
        return;
    }
    KeyMsg km;
    while (n > 0) {
        km.len = (uint8_t)(n > sizeof(km.data) ? sizeof(km.data) : n);
        memcpy(km.data, seq, km.len);
        xQueueSend((QueueHandle_t)keys_, &km, 0);
        seq += km.len;
        n -= km.len;
    }
}

void SshSession::passwordChar(char c)
{
    prompt_.charIn(c);
}

void SshSession::passwordBackspace(void)
{
    prompt_.backspace();
}

// Reset the local password prompt and drop any queued password. Called by the
// main loop on the transition into SSH_STATE_AUTH, so prompt_ (owned by the
// main loop) is never touched from the SSH task. A stale password from a
// previous connection can then never auto-authenticate the new one.
void SshSession::passwordInit(void)
{
    prompt_.init();
    if (pw_ != nullptr) {
        xQueueReset((QueueHandle_t)pw_);
    }
}

void SshSession::passwordSubmit(void)
{
    char pw[TERM_PASSWD_MAX];
    size_t n = 0;
    if (!prompt_.commit(pw, sizeof(pw), &n) || n == 0) {
        return;
    }
    if (pw_ == nullptr) {
        return;
    }
    char item[TERM_PASSWD_MAX];
    memcpy(item, pw, n);
    if (n < sizeof(item)) {
        memset(item + n, 0, sizeof(item) - n);
    }
    xQueueSend((QueueHandle_t)pw_, item, 0);
}

size_t SshSession::passwordMask(char *out, size_t cap) const
{
    return prompt_.mask(out, cap);
}

size_t SshSession::rxAvailable(void) const
{
    if (rx_ == nullptr) {
        return 0;
    }
    return xStreamBufferBytesAvailable((StreamBufferHandle_t)rx_);
}

size_t SshSession::rxRead(uint8_t *dst, size_t cap)
{
    if (rx_ == nullptr) {
        return 0;
    }
    return xStreamBufferReceive((StreamBufferHandle_t)rx_, dst, cap, 0);
}

const char *SshSession::stateText(void) const
{
    switch (state_) {
    case SSH_STATE_WIFI:
        return "wifi connecting...";
    case SSH_STATE_CONNECT:
        return "connecting...";
    case SSH_STATE_AUTH:
        return "password:";
    case SSH_STATE_AUTHING:
        return "authenticating...";
    case SSH_STATE_SHELL:
        return "connected";
    case SSH_STATE_RETRY:
        return "disconnected - retrying...";
    default:
        return "";
    }
}