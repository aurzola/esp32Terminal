#include "term.h"

#include <string.h>

void TermGrid::init(uint8_t *scrollMem)
{
    scroll_ = scrollMem;
    scrollHead_ = 0;
    scrollCount_ = 0;
    escState_ = 0;
    csiPrivate_ = 0;
    altCharset_ = 0;
    cur_ = &main_;
    savedR_ = 0;
    savedC_ = 0;
    savedAttr_ = TERM_ATTR_NONE;
    resetRegion();
    clear();
}

void TermGrid::resetRegion(void)
{
    regionTop_ = 0;
    regionBottom_ = TERM_ROWS - 1;
}

void TermGrid::clear(void)
{
    cur_ = &main_;
    for (int r = 0; r < TERM_ROWS; r++) {
        memset(cur_->text[r], ' ', TERM_COLS);
        cur_->text[r][TERM_COLS] = '\0';
        memset(cur_->attr[r], TERM_ATTR_NONE, TERM_COLS);
    }
    curR_ = 0;
    curC_ = 0;
    curAttr_ = TERM_ATTR_NONE;
    resetRegion();
    dirtyAll();
}

void TermGrid::feed(const uint8_t *data, size_t n)
{
    // States: 0 = text, 1 = after ESC, 2 = in CSI, 3 = in OSC text,
    // 4 = charset selector (after ESC(/ESC)).
    // Unhandled sequences (ESC + non-'[', or a CSI with an unknown final
    // byte, or an OSC payload) are consumed whole and dropped, so the grid
    // is never corrupted by them.
    for (size_t i = 0; i < n; i++) {
        uint8_t c = data[i];
        switch (escState_) {
        case 0:
            if (c == 0x1b) {
                escState_ = 1;
            } else {
                putChar((char)c);
            }
            break;
        case 1:
            if (c == '[') {
                csiParamCount_ = 0;
                csiParams_[0] = -1;
                csiPrivate_ = 0;
                escState_ = 2;
            } else if (c == ']') {
                escState_ = 3; // OSC: swallow until BEL
            } else if (c == '(' || c == ')') {
                escState_ = 4;
            } else if (c == 0x1b) {
                escState_ = 1;
            } else {
                escState_ = 0; // ESC + single letter: consume and drop
            }
            break;
        case 2:
            if (c == '?') {
                csiPrivate_ = 1;
            } else if (c >= '0' && c <= '9') {
                int &p = csiParams_[csiParamCount_ >= 8 ? 7 : csiParamCount_];
                if (p < 0) p = 0;
                p = p * 10 + (c - '0');
            } else if (c == ';') {
                if (csiParamCount_ < 7) {
                    csiParamCount_++;
                    csiParams_[csiParamCount_] = -1;
                }
            } else if (c >= '@' && c <= '~') {
                execCsi((char)c);
                escState_ = 0;
            } else {
                escState_ = 0; // stray byte: abort sequence, keep grid intact
            }
            break;
        case 3:
            if (c == 0x07 || c == 0x1b || c == 0x9c) {
                escState_ = 0;
            }
            break;
        case 4:
            if (c == '0') {
                altCharset_ = 1;
            } else if (c == 'B') {
                altCharset_ = 0;
            }
            escState_ = 0;
            break;
        }
    }
}

void TermGrid::execCsi(char final)
{
    const int nparam = csiParamCount_ + 1;
    auto param = [&](int idx) {
        if (idx >= 0 && idx < nparam && csiParams_[idx] >= 0) {
            return csiParams_[idx];
        }
        return -1;
    };

    switch (final) {
    case 'H':
    case 'f': {
        int r = param(0) >= 1 ? param(0) - 1 : 0;
        int c = param(1) >= 1 ? param(1) - 1 : 0;
        curR_ = r < TERM_ROWS ? r : TERM_ROWS - 1;
        curC_ = c < TERM_COLS ? c : TERM_COLS - 1;
        break;
    }
    case 'A': {
        int n = param(0) >= 1 ? param(0) : 1;
        curR_ -= n;
        if (curR_ < regionTop_) curR_ = regionTop_;
        break;
    }
    case 'B': {
        int n = param(0) >= 1 ? param(0) : 1;
        curR_ += n;
        if (curR_ > regionBottom_) curR_ = regionBottom_;
        break;
    }
    case 'C': {
        int n = param(0) >= 1 ? param(0) : 1;
        curC_ += n;
        if (curC_ >= TERM_COLS) curC_ = TERM_COLS - 1;
        break;
    }
    case 'D': {
        int n = param(0) >= 1 ? param(0) : 1;
        curC_ -= n;
        if (curC_ < 0) curC_ = 0;
        break;
    }
    case 'J': {
        int mode = param(0) >= 0 ? param(0) : 0;
        switch (mode) {
        case 0:
            eraseRange(curR_, curC_, TERM_ROWS - 1, TERM_COLS - 1);
            break;
        case 1:
            eraseRange(0, 0, curR_, curC_);
            break;
        case 2:
            eraseRange(0, 0, TERM_ROWS - 1, TERM_COLS - 1);
            if (!inAlt()) {
                scrollHead_ = 0;
                scrollCount_ = 0;
            }
            break;
        }
        break;
    }
    case 'K': {
        int mode = param(0) >= 0 ? param(0) : 0;
        switch (mode) {
        case 0:
            eraseRange(curR_, curC_, curR_, TERM_COLS - 1);
            break;
        case 1:
            eraseRange(curR_, 0, curR_, curC_);
            break;
        case 2:
            eraseRange(curR_, 0, curR_, TERM_COLS - 1);
            break;
        }
        break;
    }
    case 'm': {
        for (int i = 0; i < nparam; i++) {
            int p = csiParams_[i];
            if (p < 0 || p == 0) {
                curAttr_ = TERM_ATTR_NONE;
            } else if (p == 1) {
                curAttr_ = (uint8_t)(curAttr_ | TERM_ATTR_BOLD);
            } else if (p == 2 || p == 90) {
                // Dim SGR, and the "bright black" fg fish uses for its
                // autosuggestion: both render the cell at reduced intensity so
                // a suggestion is distinguishable from typed text.
                curAttr_ = (uint8_t)(curAttr_ | TERM_ATTR_DIM);
            } else if (p == 7) {
                curAttr_ = (uint8_t)(curAttr_ | TERM_ATTR_INVERSE);
            } else if (p == 22) {
                curAttr_ = (uint8_t)(curAttr_ & ~(TERM_ATTR_BOLD | TERM_ATTR_DIM));
            } else if (p == 27) {
                curAttr_ = (uint8_t)(curAttr_ & ~TERM_ATTR_INVERSE);
            }
        }
        break;
    }
    case 'r': {
        int top = param(0) >= 1 ? param(0) : 1;
        int bot = param(1) >= 1 ? param(1) : TERM_ROWS;
        if (top < 1) top = 1;
        if (bot > TERM_ROWS) bot = TERM_ROWS;
        if (top >= bot) {
            resetRegion();
        } else {
            regionTop_ = top - 1;
            regionBottom_ = bot - 1;
        }
        break;
    }
    case 'h':
        if (csiPrivate_ && param(0) == 1049) {
            enterAlt();
        }
        break;
    case 'l':
        if (csiPrivate_ && param(0) == 1049) {
            exitAlt();
        }
        break;
    default:
        break;
    }
}

void TermGrid::eraseRange(int r0, int c0, int r1, int c1)
{
    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            eraseCell(r, c);
        }
    }
}

void TermGrid::enterAlt(void)
{
    savedR_ = curR_;
    savedC_ = curC_;
    savedAttr_ = curAttr_;
    cur_ = &alt_;
    curR_ = 0;
    curC_ = 0;
    eraseRange(0, 0, TERM_ROWS - 1, TERM_COLS - 1);
}

void TermGrid::exitAlt(void)
{
    if (cur_ != &alt_) {
        return;
    }
    cur_ = &main_;
    curR_ = savedR_;
    curC_ = savedC_;
    curAttr_ = savedAttr_;
    dirtyAll();
}

void TermGrid::putChar(char c)
{
    if (c == '\r') {
        curC_ = 0;
        return;
    }
    if (c == '\n') {
        newline();
        return;
    }
    if (c == '\b') {
        if (curC_ > 0) {
            curC_--;
        }
        return;
    }
    if (c < 0x20 || c > 0x7e) {
        return;
    }
    if (curC_ >= TERM_COLS) {
        newline();
    }
    cur_->text[curR_][curC_] = c;
    cur_->attr[curR_][curC_] =
        (uint8_t)(curAttr_ | (altCharset_ ? TERM_ATTR_ALTCHARS : 0));
    cur_->dirty[curR_] = 1;
    curC_++;
}

void TermGrid::eraseCell(int r, int c)
{
    cur_->text[r][c] = ' ';
    cur_->attr[r][c] = curAttr_;
    cur_->dirty[r] = 1;
}

void TermGrid::newline(void)
{
    if (curR_ < regionBottom_) {
        curR_++;
        curC_ = 0;
        return;
    }
    if (curR_ > regionBottom_ && curR_ < TERM_ROWS - 1) {
        curR_++;
        curC_ = 0;
        return;
    }
    if (!inAlt() && scroll_ != nullptr) {
        uint8_t *dst = scroll_ + (scrollHead_ % TERM_SCROLLBACK_LINES) * TERM_COLS;
        memcpy(dst, cur_->text[regionTop_], TERM_COLS);
        scrollHead_++;
        if (scrollCount_ < TERM_SCROLLBACK_LINES) {
            scrollCount_++;
        }
    }
    for (int r = regionTop_; r < regionBottom_; r++) {
        memcpy(cur_->text[r], cur_->text[r + 1], (size_t)(TERM_COLS + 1));
        memcpy(cur_->attr[r], cur_->attr[r + 1], (size_t)TERM_COLS);
    }
    memset(cur_->text[regionBottom_], ' ', TERM_COLS);
    cur_->text[regionBottom_][TERM_COLS] = '\0';
    memset(cur_->attr[regionBottom_], TERM_ATTR_NONE, TERM_COLS);
    curC_ = 0;
    dirtyAll();
}

const char *TermGrid::rowText(int r) const
{
    return cur_->text[r];
}

const uint8_t *TermGrid::rowAttr(int r) const
{
    return cur_->attr[r];
}

bool TermGrid::isRowDirty(int r) const
{
    return cur_->dirty[r] != 0;
}

void TermGrid::clearRowDirty(int r)
{
    cur_->dirty[r] = 0;
}

void TermGrid::dirtyAll(void)
{
    memset(cur_->dirty, 1, sizeof(cur_->dirty));
}