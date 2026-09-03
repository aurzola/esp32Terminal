#include "passprompt.h"

#include <string.h>

void PassPrompt::init(void)
{
    len_ = 0;
}

void PassPrompt::charIn(char c)
{
    if (len_ < TERM_PASSWD_MAX) {
        buf_[len_++] = c;
    }
}

void PassPrompt::backspace(void)
{
    if (len_ > 0) {
        len_--;
    }
}

size_t PassPrompt::mask(char *out, size_t cap) const
{
    size_t n = len_ < cap ? len_ : cap - 1;
    memset(out, '*', n);
    out[n] = '\0';
    return len_;
}

bool PassPrompt::commit(char *out, size_t cap, size_t *n)
{
    if (len_ == 0) {
        if (n != nullptr) {
            *n = 0;
        }
        return false;
    }
    size_t copy = len_ < cap ? len_ : cap;
    memcpy(out, buf_, copy);
    if (copy < cap) {
        out[copy] = '\0';
    }
    if (n != nullptr) {
        *n = copy;
    }
    len_ = 0;
    return true;
}