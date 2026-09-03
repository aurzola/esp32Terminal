#ifndef TERM_PASSPROMPT_H
#define TERM_PASSPROMPT_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"

// Local password prompt (ticket 05): collects the SSH password typed on the
// keyboard, shown masked on the CRT, and only hands it to the session on
// commit. Pure and host-testable: no hardware, no libssh.
class PassPrompt {
public:
    void init(void);
    void charIn(char c);                       // append a printable char
    void backspace(void);                      // drop the last char
    size_t length(void) const { return len_; }
    // Fills `out` with asterisks (at most cap-1, NUL-terminated) and returns
    // the real password length; callers truncate long passwords for display.
    size_t mask(char *out, size_t cap) const;
    bool commit(char *out, size_t cap, size_t *n); // copy real chars, reset

private:
    char buf_[TERM_PASSWD_MAX];
    size_t len_;
};

#endif