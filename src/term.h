#ifndef TERM_H
#define TERM_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"

enum TermAttr : uint8_t {
    TERM_ATTR_NONE = 0,
    TERM_ATTR_BOLD = 1,
    TERM_ATTR_INVERSE = 2,
    TERM_ATTR_ALTCHARS = 4,
    TERM_ATTR_DIM = 8,
};

class TermGrid {
public:
    void init(uint8_t *scrollMem);
    void clear(void);
    void feed(const uint8_t *data, size_t n);

    const char *rowText(int r) const;
    const uint8_t *rowAttr(int r) const;
    bool isRowDirty(int r) const;
    void clearRowDirty(int r);

    int cursorRow(void) const { return curR_; }
    int cursorCol(void) const { return curC_; }
    size_t scrollbackLines(void) const { return scrollCount_; }

private:
    struct Cellbuf {
        char text[TERM_ROWS][TERM_COLS + 1];
        uint8_t attr[TERM_ROWS][TERM_COLS];
        uint8_t dirty[TERM_ROWS];
    };

    void putChar(char c);
    void newline(void);
    void dirtyAll(void);
    void eraseCell(int r, int c);
    void eraseRange(int r0, int c0, int r1, int c1);
    void execCsi(char final);
    void enterAlt(void);
    void exitAlt(void);
    void resetRegion(void);
    bool inAlt(void) const { return cur_ == &alt_; }

    Cellbuf main_;
    Cellbuf alt_;
    Cellbuf *cur_;
    int curR_;
    int curC_;
    uint8_t curAttr_;

    uint8_t *scroll_;
    size_t scrollHead_;
    size_t scrollCount_;

    int csiParams_[8];
    int csiParamCount_;
    int escState_;
    uint8_t csiPrivate_;
    int regionTop_;
    int regionBottom_;

    int savedR_;
    int savedC_;
    uint8_t savedAttr_;
    uint8_t altCharset_;
};

#endif