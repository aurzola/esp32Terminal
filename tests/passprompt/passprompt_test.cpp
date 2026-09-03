// Host unit tests for the local SSH password prompt (no Arduino, no hardware).
// Seam: the pure PassPrompt class and the sshBackoffDelayMs helper. Compile:
//   g++ -std=c++17 -I ../../src -o /tmp/passprompt_test \
//       passprompt_test.cpp ../../src/passprompt.cpp

#include <cstdio>
#include <cstring>

#include "passprompt.h"
#include "backoff.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

int main(void)
{
    // ---- a fresh prompt is empty and masks as nothing
    {
        PassPrompt p;
        p.init();
        CHECK(p.length() == 0);
        char m[TERM_PASSWD_MAX + 1];
        CHECK(p.mask(m, sizeof(m)) == 0);
        CHECK(m[0] == '\0');
    }

    // ---- chars accumulate; the mask shows one asterisk per char
    {
        PassPrompt p;
        p.init();
        p.charIn('s');
        p.charIn('3');
        p.charIn('c');
        CHECK(p.length() == 3);
        char m[TERM_PASSWD_MAX + 1];
        CHECK(p.mask(m, sizeof(m)) == 3);
        CHECK(strcmp(m, "***") == 0);
    }

    // ---- backspace pops the last char and shrinks the mask
    {
        PassPrompt p;
        p.init();
        p.charIn('a');
        p.charIn('b');
        p.backspace();
        CHECK(p.length() == 1);
        char m[TERM_PASSWD_MAX + 1];
        p.mask(m, sizeof(m));
        CHECK(strcmp(m, "*") == 0);
        p.backspace();
        CHECK(p.length() == 0);
        p.backspace(); // backspace on empty is a no-op
        CHECK(p.length() == 0);
    }

    // ---- commit copies the real chars (not the mask) and clears the prompt
    {
        PassPrompt p;
        p.init();
        p.charIn('x');
        p.charIn('y');
        char out[TERM_PASSWD_MAX + 1];
        size_t n = 0;
        CHECK(p.commit(out, sizeof(out), &n));
        CHECK(n == 2);
        CHECK(strcmp(out, "xy") == 0);
        CHECK(p.length() == 0); // prompt reset after commit
    }

    // ---- commit on an empty prompt fails without writing
    {
        PassPrompt p;
        p.init();
        char out[TERM_PASSWD_MAX + 1];
        memset(out, 0x5a, sizeof(out));
        size_t n = 99;
        CHECK(!p.commit(out, sizeof(out), &n));
        CHECK(n == 0);
        CHECK(out[0] == 0x5a); // untouched
    }

    // ---- the buffer caps at TERM_PASSWD_MAX chars, ignoring the overflow
    {
        PassPrompt p;
        p.init();
        for (int i = 0; i < TERM_PASSWD_MAX + 20; i++) {
            p.charIn('q');
        }
        CHECK(p.length() == (size_t)TERM_PASSWD_MAX);
        char out[TERM_PASSWD_MAX + 1];
        size_t n = 0;
        CHECK(p.commit(out, sizeof(out), &n));
        CHECK(n == (size_t)TERM_PASSWD_MAX);
        CHECK(out[TERM_PASSWD_MAX - 1] == 'q');
        // a commit into an undersized buffer never overflows
        char tiny[3];
        memset(tiny, 0x5a, sizeof(tiny));
        PassPrompt q;
        q.init();
        q.charIn('z');
        q.charIn('z');
        CHECK(q.commit(tiny, sizeof(tiny), &n));
        CHECK(n == 2);
        CHECK(strncmp(tiny, "zz", 2) == 0);
    }

    // ---- reconnect backoff: starts at the base and doubles up to the cap
    {
        CHECK(sshBackoffDelayMs(0) == TERM_SSH_BACKOFF_BASE_MS);
        CHECK(sshBackoffDelayMs(1) == 2 * TERM_SSH_BACKOFF_BASE_MS);
        CHECK(sshBackoffDelayMs(2) == 4 * TERM_SSH_BACKOFF_BASE_MS);
        // grows until the cap, then stays capped
        uint32_t prev = 0;
        for (int a = 0; a < 16; a++) {
            uint32_t d = sshBackoffDelayMs(a);
            CHECK(d >= prev); // never decreases
            CHECK(d <= TERM_SSH_BACKOFF_MAX_MS);
            prev = d;
        }
        // the cap is reachable and stable
        CHECK(sshBackoffDelayMs(20) == TERM_SSH_BACKOFF_MAX_MS);
        CHECK(sshBackoffDelayMs(21) == TERM_SSH_BACKOFF_MAX_MS);
    }

    if (g_failures == 0) {
        printf("passprompt: all tests passed\n");
        return 0;
    }
    printf("passprompt: %d FAILURES\n", g_failures);
    return 1;
}