#ifndef TERM_SSH_H
#define TERM_SSH_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "passprompt.h"
#include "backoff.h"

enum SshState {
    SSH_STATE_OFF = 0,
    SSH_STATE_WIFI,      // connecting WiFi
    SSH_STATE_CONNECT,   // TCP/SSH handshake
    SSH_STATE_AUTH,      // waiting for the password on the CRT
    SSH_STATE_AUTHING,   // authenticating
    SSH_STATE_SHELL,     // interactive shell up
    SSH_STATE_RETRY,     // disconnected, backing off
};

// Hardware-bound SSH session (WiFi + libssh) for ticket 05. Runs its own task
// because libssh blocks (connect/auth/read); the main loop keeps the video
// going and only drains the rx ring into the grid. Like kbd.cpp, this module
// is not unit-tested on the host: the pure seams upstream (PassPrompt for the
// masked password, sshBackoffDelayMs for the reconnect backoff) carry the
// logic that is verified in tests/passprompt.
class SshSession {
public:
    void begin(void);                        // start the session task

    SshState state(void) const { return state_; }
    const char *stateText(void) const;       // ASCII text for the Estado line
    bool shellActive(void) const { return state_ == SSH_STATE_SHELL; }
    bool authWait(void) const { return state_ == SSH_STATE_AUTH; }

    // main loop -> session (keyboard)
    void sendKey(const uint8_t *seq, size_t n);   // shell keystrokes
    void passwordInit(void);                       // reset the local prompt
    void passwordChar(char c);
    void passwordBackspace(void);
    void passwordSubmit(void);
    size_t passwordMask(char *out, size_t cap) const;

    // session -> main loop (host bytes)
    size_t rxAvailable(void) const;
    size_t rxRead(uint8_t *dst, size_t cap);

private:
    static void taskMain(void *arg);
    void run(void);
    bool connectWifi(void);
    bool waitPassword(char *pw, void *session);
    void pumpChannel(void *session, void *channel);
    void retryDelay(void);

    volatile SshState state_;
    int attempt_;
    PassPrompt prompt_;
    void *rx_;     // StreamBufferHandle_t
    void *keys_;   // QueueHandle_t of KeyMsg
    void *pw_;     // QueueHandle_t of char[TERM_PASSWD_MAX]
};

extern SshSession ssh;

#endif