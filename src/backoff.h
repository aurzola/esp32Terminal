#ifndef TERM_BACKOFF_H
#define TERM_BACKOFF_H

#include <stdint.h>

#include "config.h"

// Exponential reconnect backoff (ms) for the SSH session, capped at
// TERM_SSH_BACKOFF_MAX_MS. attempt counts successive failed reconnect tries.
// Pure and host-testable.
static inline uint32_t sshBackoffDelayMs(int attempt)
{
    if (attempt <= 0) {
        return TERM_SSH_BACKOFF_BASE_MS;
    }
    uint64_t d = (uint64_t)TERM_SSH_BACKOFF_BASE_MS
                 << (attempt > 20 ? 20 : attempt);
    if (d > TERM_SSH_BACKOFF_MAX_MS) {
        return TERM_SSH_BACKOFF_MAX_MS;
    }
    return (uint32_t)d;
}

#endif