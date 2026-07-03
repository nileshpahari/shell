/*
 * editor.c -- Custom line editor based on linenoise.
 *
 * A simplified adaptation of the linenoise line editing library
 * (http://github.com/antirez/linenoise) for use as a shell line editor.
 *
 * Kept from linenoise:
 *   - Raw mode terminal control
 *   - Single-line editing with horizontal scroll
 *   - UTF-8 character handling and display width
 *   - ANSI escape-aware prompt width calculation
 *   - History navigation
 *   - Standard editing keybindings (Ctrl-A/E/K/U/W/T/L, arrows, etc.)
 *   - Non-TTY fallback for piped input
 *
 * Removed from linenoise:
 *   - Emoji / grapheme cluster support (ZWJ, skin tones, regional indicators)
 *   - Fold system (paste display folding)
 *   - Bracketed paste mode
 *   - Async / multiplexed API
 *   - Tab completion and hints callbacks
 *   - Mask mode (password input)
 *   - Multi-line editing mode
 *   - History save/load to file
 *   - Dynamic buffer growing
 *
 * Original linenoise copyright:
 *   Copyright (c) 2010-2023, Salvatore Sanfilippo <antirez at gmail dot com>
 *   Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *   BSD 2-Clause License (see linenoise/LICENSE for full text).
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "../include/editor.h"

/* ========================== Configuration ================================= */

#define EDITOR_MAX_LINE 4096
#define EDITOR_DEFAULT_HISTORY_MAX_LEN 100

/* ========================== Internal Types ================================ */

/* Editing state passed to every editing helper function. */
struct editorState {
    int ifd;            /* Terminal stdin file descriptor. */
    int ofd;            /* Terminal stdout file descriptor. */
    char *buf;          /* Edited line buffer. */
    size_t buflen;      /* Usable buffer size (excluding nul). */
    const char *prompt; /* Full prompt (written once at start). */
    size_t plen;        /* Full prompt length in bytes. */
    const char *rprompt; /* Last line of prompt (used by refreshLine). */
    size_t rplen;        /* Last-line prompt length in bytes. */
    size_t pos;         /* Current cursor position (byte offset). */
    size_t len;         /* Current edited line length (bytes). */
    size_t cols;        /* Number of columns in terminal. */
    int history_index;  /* The history index we are currently editing. */
};

/* Append buffer: heap-allocated string we can append to, so that all
 * terminal escape sequences for a single refresh are flushed in one
 * write() call to avoid flickering. */
struct abuf {
    char *b;
    int len;
};

/* ========================== Static State ================================== */

static struct termios orig_termios;     /* Saved terminal attributes. */
static int rawmode = 0;                /* True when terminal is in raw mode. */
static int atexit_registered = 0;      /* atexit handler registered once. */
static int history_max_len = EDITOR_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

static char *unsupported_term[] = {"dumb", "cons25", "emacs", NULL};

/* ========================== Key Actions =================================== */

enum KEY_ACTION {
    KEY_NULL  = 0,
    CTRL_A    = 1,         /* Go to start of line */
    CTRL_B    = 2,         /* Move cursor left */
    CTRL_C    = 3,         /* Cancel line */
    CTRL_D    = 4,         /* Delete char or EOF */
    CTRL_E    = 5,         /* Go to end of line */
    CTRL_F    = 6,         /* Move cursor right */
    CTRL_H    = 8,         /* Backspace */
    TAB       = 9,         /* Tab (ignored) */
    CTRL_K    = 11,        /* Kill to end of line */
    CTRL_L    = 12,        /* Clear screen */
    ENTER     = 13,        /* Submit line */
    CTRL_N    = 14,        /* Next history */
    CTRL_P    = 16,        /* Previous history */
    CTRL_T    = 20,        /* Transpose characters */
    CTRL_U    = 21,        /* Kill entire line */
    CTRL_W    = 23,        /* Delete previous word */
    ESC       = 27,        /* Escape / start of sequence */
    BACKSPACE = 127        /* Backspace */
};

/* ========================== Forward Declarations ========================== */

static void editorAtExit(void);
static void refreshLine(struct editorState *l);

/* ========================== UTF-8 Support ================================= */

/* Return the byte length of the UTF-8 character whose first byte is 'c'. */
static int utf8ByteLen(char c) {
    unsigned char uc = (unsigned char)c;
    if ((uc & 0x80) == 0)    return 1;   /* 0xxxxxxx: ASCII */
    if ((uc & 0xE0) == 0xC0) return 2;   /* 110xxxxx */
    if ((uc & 0xF0) == 0xE0) return 3;   /* 1110xxxx */
    if ((uc & 0xF8) == 0xF0) return 4;   /* 11110xxx */
    return 1;                             /* Invalid byte, treat as single. */
}

/* Decode a UTF-8 sequence at 's' into a Unicode codepoint.
 * Sets *len to the byte length consumed. Assumes valid encoding. */
static uint32_t utf8DecodeChar(const char *s, size_t *len) {
    unsigned char *p = (unsigned char *)s;
    uint32_t cp;

    if ((*p & 0x80) == 0) {
        *len = 1;
        return *p;
    } else if ((*p & 0xE0) == 0xC0) {
        *len = 2;
        cp  = (*p & 0x1F) << 6;
        cp |= (p[1] & 0x3F);
        return cp;
    } else if ((*p & 0xF0) == 0xE0) {
        *len = 3;
        cp  = (*p & 0x0F) << 12;
        cp |= (p[1] & 0x3F) << 6;
        cp |= (p[2] & 0x3F);
        return cp;
    } else if ((*p & 0xF8) == 0xF0) {
        *len = 4;
        cp  = (*p & 0x07) << 18;
        cp |= (p[1] & 0x3F) << 12;
        cp |= (p[2] & 0x3F) << 6;
        cp |= (p[3] & 0x3F);
        return cp;
    }
    *len = 1;
    return *p;   /* Fallback for invalid sequences. */
}

/* Return byte length of the next UTF-8 character at buf[pos]. */
static size_t edNextCharLen(const char *buf, size_t pos, size_t len) {
    if (pos >= len) return 0;
    return (size_t)utf8ByteLen(buf[pos]);
}

/* Return byte length of the UTF-8 character ending just before buf[pos].
 * Scans backwards through continuation bytes (10xxxxxx) to find the
 * start byte. */
static size_t edPrevCharLen(const char *buf, size_t pos) {
    if (pos == 0) return 0;
    size_t i = pos - 1;
    while (i > 0 && ((unsigned char)buf[i] & 0xC0) == 0x80)
        i--;
    return pos - i;
}

/* If s[] points at an ANSI CSI escape sequence (e.g. "\033[1;32m"),
 * return its total byte length. Otherwise return 0.
 * Layout: ESC '[' , parameter bytes (0x30–0x3f), intermediate bytes
 * (0x20–0x2f), final byte (0x40–0x7e). */
static size_t ansiEscapeLen(const char *s, size_t len) {
    size_t i;
    if (len < 2 || s[1] != '[') return 0;
    i = 2;
    while (i < len && (unsigned char)s[i] >= 0x30 &&
           (unsigned char)s[i] <= 0x3f)
        i++;
    while (i < len && (unsigned char)s[i] >= 0x20 &&
           (unsigned char)s[i] <= 0x2f)
        i++;
    if (i >= len || (unsigned char)s[i] < 0x40 ||
        (unsigned char)s[i] > 0x7e)
        return 0;
    return i + 1;
}

/* Return the terminal display width of a single Unicode codepoint.
 *   - Control chars and combining marks: 0 columns
 *   - CJK ideographs and fullwidth forms: 2 columns
 *   - Everything else: 1 column */
static int utf8CharWidth(uint32_t cp) {
    /* Control characters: zero width. */
    if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;

    /* Combining diacritical marks: zero width. */
    if ((cp >= 0x0300 && cp <= 0x036F) ||
        (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) ||
        (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE20 && cp <= 0xFE2F))
        return 0;

    /* Wide characters: CJK and fullwidth forms. */
    if (cp >= 0x1100 &&
        (cp <= 0x115F ||
         (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
         (cp >= 0xAC00 && cp <= 0xD7A3) ||
         (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFE10 && cp <= 0xFE19) ||
         (cp >= 0xFE30 && cp <= 0xFE6F) ||
         (cp >= 0xFF00 && cp <= 0xFF60) ||
         (cp >= 0xFFE0 && cp <= 0xFFE6) ||
         (cp >= 0x20000 && cp <= 0x2FFFF)))
        return 2;

    return 1;
}

/* Calculate the display width of a UTF-8 string of 'len' bytes.
 * ANSI CSI escape sequences (e.g. color codes) are skipped and
 * contribute zero width. */
static size_t utf8StrWidth(const char *s, size_t len) {
    size_t width = 0;
    size_t i = 0;

    while (i < len) {
        size_t clen;
        uint32_t cp = utf8DecodeChar(s + i, &clen);

        /* Skip ANSI escape sequences entirely. */
        if (cp == 0x1b) {
            size_t skip = ansiEscapeLen(s + i, len - i);
            if (skip > 0) {
                i += skip;
                continue;
            }
        }

        width += utf8CharWidth(cp);
        i += clen;
    }
    return width;
}

/* ======================= Low-Level Terminal Handling ======================= */

/* Return true if the TERM environment variable names a terminal that
 * cannot handle the escape sequences we use. */
static int isUnsupportedTerm(void) {
    char *term = getenv("TERM");
    if (term == NULL) return 0;
    for (int j = 0; unsupported_term[j]; j++)
        if (!strcasecmp(term, unsupported_term[j])) return 1;
    return 0;
}

/* Put the terminal into raw mode so we receive each keystroke
 * immediately, without echo or line buffering. */
static int enableRawMode(int fd) {
    struct termios raw;

    if (!isatty(STDIN_FILENO)) goto fatal;
    if (!atexit_registered) {
        atexit(editorAtExit);
        atexit_registered = 1;
    }
    if (tcgetattr(fd, &orig_termios) == -1) goto fatal;

    raw = orig_termios;
    /* Input: no break, no CR-to-NL, no parity, no strip, no flow ctrl. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* Output: disable post-processing. */
    raw.c_oflag &= ~(OPOST);
    /* Control: 8-bit characters. */
    raw.c_cflag |= (CS8);
    /* Local: no echo, no canonical, no extended, no signals. */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* Return after every single byte, no timeout. */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSAFLUSH, &raw) < 0) goto fatal;
    rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

/* Restore the terminal to its original (cooked) mode. */
static void disableRawMode(int fd) {
    if (rawmode && tcsetattr(fd, TCSAFLUSH, &orig_termios) != -1)
        rawmode = 0;
}

/* Query the cursor position using the DSR escape sequence.
 * Returns the column (1-based) or -1 on error. */
static int getCursorPosition(int ifd, int ofd) {
    char buf[32];
    int cols, rows;
    unsigned int i = 0;

    if (write(ofd, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(ifd, buf + i, 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != ESC || buf[1] != '[') return -1;
    if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2) return -1;
    return cols;
}

/* Try to get the terminal width. Falls back to the cursor-probe
 * method if ioctl fails, and to 80 as a last resort. */
static int getColumns(int ifd, int ofd) {
    struct winsize ws;

    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        int start, cols;

        start = getCursorPosition(ifd, ofd);
        if (start == -1) goto failed;

        if (write(ofd, "\x1b[999C", 6) != 6) goto failed;
        cols = getCursorPosition(ifd, ofd);
        if (cols == -1) goto failed;

        /* Restore original cursor position. */
        if (cols > start) {
            char seq[32];
            snprintf(seq, 32, "\x1b[%dD", cols - start);
            if (write(ofd, seq, strlen(seq)) == -1) { /* ignore */ }
        }
        return cols;
    } else {
        return ws.ws_col;
    }

failed:
    return 80;
}

/* Clear the terminal screen and position cursor at top-left. */
void editor_clear_screen(void) {
    if (write(STDOUT_FILENO, "\x1b[H\x1b[2J", 7) <= 0) { /* ignore */ }
}

/* ========================== Append Buffer ================================= */

static void abInit(struct abuf *ab) {
    ab->b = NULL;
    ab->len = 0;
}

static void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL) return;
    memcpy(new + ab->len, s, len);
    ab->b = new;
    ab->len += len;
}

static void abFree(struct abuf *ab) {
    free(ab->b);
}

/* ========================== Line Refresh ================================== */

/* Single-line refresh: rewrite the prompt and buffer on the current
 * terminal row, with horizontal scrolling when the content exceeds
 * the terminal width. All output is batched into a single write(). */
static void refreshLine(struct editorState *l) {
    char seq[64];
    /* Use rprompt (last line only) so \r stays on the input row. */
    size_t pwidth = utf8StrWidth(l->rprompt, l->rplen);
    int fd = l->ofd;
    char *buf = l->buf;
    size_t len = l->len;
    size_t pos = l->pos;
    size_t poscol;   /* Display column of cursor. */
    size_t lencol;   /* Display width of visible buffer. */
    struct abuf ab;

    poscol = utf8StrWidth(buf, pos);
    lencol = utf8StrWidth(buf, len);

    /* Scroll left: trim whole UTF-8 characters from the start until
     * the cursor fits within the terminal width. */
    while (pwidth + poscol >= l->cols) {
        size_t clen;
        uint32_t cp = utf8DecodeChar(buf, &clen);
        int cwidth = utf8CharWidth(cp);
        buf += clen;
        len -= clen;
        pos -= clen;
        poscol -= cwidth;
        lencol -= cwidth;
    }

    /* Trim from the right if the line still overflows. */
    while (pwidth + lencol > l->cols) {
        size_t clen = edPrevCharLen(buf, len);
        size_t dummy;
        uint32_t cp = utf8DecodeChar(buf + len - clen, &dummy);
        int cwidth = utf8CharWidth(cp);
        len -= clen;
        lencol -= cwidth;
    }

    abInit(&ab);

    /* Cursor to left edge. */
    abAppend(&ab, "\r", 1);

    /* Write last-line prompt + visible portion of buffer. */
    abAppend(&ab, l->rprompt, l->rplen);
    abAppend(&ab, buf, len);

    /* Erase everything to the right of our output. */
    snprintf(seq, sizeof(seq), "\x1b[0K");
    abAppend(&ab, seq, strlen(seq));

    /* Position cursor at the correct column. */
    snprintf(seq, sizeof(seq), "\r\x1b[%dC", (int)(poscol + pwidth));
    abAppend(&ab, seq, strlen(seq));

    if (write(fd, ab.b, ab.len) == -1) { /* Can't recover. */ }
    abFree(&ab);
}

/* ========================== Editing Operations ============================ */

/* Insert character(s) 'c' of byte length 'clen' at the cursor position. */
static int editorEditInsert(struct editorState *l, const char *c, size_t clen) {
    if (l->len + clen >= l->buflen) return -1;

    if (l->len == l->pos) {
        /* Appending at end of line. */
        memcpy(l->buf + l->pos, c, clen);
        l->pos += clen;
        l->len += clen;
        l->buf[l->len] = '\0';

        /* Fast path: if prompt + buffer still fits in one line,
         * just write the new bytes without a full refresh. */
        size_t bufwidth = utf8StrWidth(l->buf, l->len);
        if (utf8StrWidth(l->rprompt, l->rplen) + bufwidth < l->cols) {
            if (write(l->ofd, c, clen) == -1) return -1;
            return 0;
        }
    } else {
        /* Inserting in the middle: shift the tail right. */
        memmove(l->buf + l->pos + clen, l->buf + l->pos, l->len - l->pos);
        memcpy(l->buf + l->pos, c, clen);
        l->pos += clen;
        l->len += clen;
        l->buf[l->len] = '\0';
    }
    refreshLine(l);
    return 0;
}

/* Move cursor left by one UTF-8 character. */
static void editorEditMoveLeft(struct editorState *l) {
    if (l->pos > 0) {
        l->pos -= edPrevCharLen(l->buf, l->pos);
        refreshLine(l);
    }
}

/* Move cursor right by one UTF-8 character. */
static void editorEditMoveRight(struct editorState *l) {
    if (l->pos != l->len) {
        l->pos += edNextCharLen(l->buf, l->pos, l->len);
        refreshLine(l);
    }
}

/* Move cursor to the start of the line. */
static void editorEditMoveHome(struct editorState *l) {
    if (l->pos != 0) {
        l->pos = 0;
        refreshLine(l);
    }
}

/* Move cursor to the end of the line. */
static void editorEditMoveEnd(struct editorState *l) {
    if (l->pos != l->len) {
        l->pos = l->len;
        refreshLine(l);
    }
}

/* Navigate history. dir is EDITOR_HISTORY_PREV (1) or _NEXT (0). */
#define EDITOR_HISTORY_NEXT 0
#define EDITOR_HISTORY_PREV 1

static void editorEditHistoryNext(struct editorState *l, int dir) {
    if (history_len > 1) {
        /* Save the current buffer back into its history slot. */
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = strdup(l->buf);

        l->history_index += (dir == EDITOR_HISTORY_PREV) ? 1 : -1;
        if (l->history_index < 0) {
            l->history_index = 0;
            return;
        } else if (l->history_index >= history_len) {
            l->history_index = history_len - 1;
            return;
        }

        /* Copy the selected history entry into the edit buffer. */
        const char *src = history[history_len - 1 - l->history_index];
        size_t slen = strlen(src);
        if (slen >= l->buflen) slen = l->buflen - 1;
        memcpy(l->buf, src, slen);
        l->buf[slen] = '\0';
        l->len = l->pos = slen;
        refreshLine(l);
    }
}

/* Delete the character at the cursor (Delete key). */
static void editorEditDelete(struct editorState *l) {
    if (l->len > 0 && l->pos < l->len) {
        size_t clen = edNextCharLen(l->buf, l->pos, l->len);
        memmove(l->buf + l->pos, l->buf + l->pos + clen,
                l->len - l->pos - clen);
        l->len -= clen;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Delete the character before the cursor (Backspace). */
static void editorEditBackspace(struct editorState *l) {
    if (l->pos > 0 && l->len > 0) {
        size_t clen = edPrevCharLen(l->buf, l->pos);
        memmove(l->buf + l->pos - clen, l->buf + l->pos, l->len - l->pos);
        l->pos -= clen;
        l->len -= clen;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Delete the word before the cursor (Ctrl+W). */
static void editorEditDeletePrevWord(struct editorState *l) {
    size_t old_pos = l->pos;
    size_t diff;

    /* Skip trailing spaces. */
    while (l->pos > 0 && l->buf[l->pos - 1] == ' ')
        l->pos -= edPrevCharLen(l->buf, l->pos);
    /* Skip non-space characters. */
    while (l->pos > 0 && l->buf[l->pos - 1] != ' ')
        l->pos -= edPrevCharLen(l->buf, l->pos);

    diff = old_pos - l->pos;
    memmove(l->buf + l->pos, l->buf + old_pos, l->len - old_pos + 1);
    l->len -= diff;
    refreshLine(l);
}

/* ========================== Non-TTY Input ================================= */

/* Read a newline-terminated line from a file/pipe with no fixed-size limit.
 * Returns a malloc'd string or NULL on EOF. */
static char *editorReadLine(FILE *fp) {
    char *line = NULL;
    size_t len = 0, cap = 0;

    while (1) {
        if (len + 1 >= cap) {
            size_t newcap = cap ? cap * 2 : 16;
            if (newcap <= cap) {
                free(line);
                errno = ENOMEM;
                return NULL;
            }
            char *tmp = realloc(line, newcap);
            if (tmp == NULL) {
                free(line);
                return NULL;
            }
            line = tmp;
            cap = newcap;
        }
        int c = fgetc(fp);
        if (c == EOF || c == '\n') {
            if (c == EOF && len == 0) {
                free(line);
                return NULL;
            }
            line[len] = '\0';
            return line;
        }
        line[len++] = c;
    }
}

/* Non-TTY entry point: just read a full line from stdin. */
static char *editorNoTTY(void) {
    return editorReadLine(stdin);
}

/* ========================== Main Edit Loop ================================ */

/* Blocking line-edit session. Puts the terminal into raw mode, handles
 * all keystrokes, and returns a malloc'd result string (or NULL). */
static char *editorEdit(int stdin_fd, int stdout_fd, char *buf,
                        size_t buflen, const char *prompt) {
    struct editorState l;

    l.ifd = stdin_fd;
    l.ofd = stdout_fd;
    l.buf = buf;
    l.buflen = buflen - 1;   /* Reserve one byte for the nul terminator. */
    l.prompt = prompt;
    l.plen = strlen(prompt);

    /* For refresh, only use the last line of the prompt. Multi-line
     * prompts have their header lines written once at the start;
     * refreshLine() only redraws the current (input) row. */
    const char *last_nl = strrchr(prompt, '\n');
    l.rprompt = last_nl ? last_nl + 1 : prompt;
    l.rplen = strlen(l.rprompt);

    l.pos = 0;
    l.len = 0;
    l.cols = getColumns(stdin_fd, stdout_fd);
    l.history_index = 0;
    l.buf[0] = '\0';

    if (enableRawMode(l.ifd) == -1) return NULL;

    /* The newest history slot is always the line currently being edited,
     * starting as an empty string. */
    editor_history_add("");

    if (write(l.ofd, prompt, l.plen) == -1) {
        disableRawMode(l.ifd);
        return NULL;
    }

    while (1) {
        char c;
        int nread;
        char seq[3];

        nread = read(l.ifd, &c, 1);
        if (nread <= 0) {
            /* EOF or read error. */
            history_len--;
            free(history[history_len]);
            disableRawMode(l.ifd);
            printf("\n");
            return NULL;
        }

        switch (c) {
        case ENTER:
            history_len--;
            free(history[history_len]);
            disableRawMode(l.ifd);
            printf("\n");
            return strdup(l.buf);

        case CTRL_C:
            history_len--;
            free(history[history_len]);
            disableRawMode(l.ifd);
            printf("\n");
            errno = EAGAIN;
            return NULL;

        case BACKSPACE:
        case CTRL_H:
            editorEditBackspace(&l);
            break;

        case CTRL_D:
            if (l.len > 0) {
                editorEditDelete(&l);
            } else {
                history_len--;
                free(history[history_len]);
                disableRawMode(l.ifd);
                printf("\n");
                errno = ENOENT;
                return NULL;
            }
            break;

        case CTRL_T:   /* Swap current character with previous. */
            if (l.pos > 0 && l.pos < l.len) {
                char tmp[4];
                size_t prevlen = edPrevCharLen(l.buf, l.pos);
                size_t currlen = edNextCharLen(l.buf, l.pos, l.len);
                size_t prevstart = l.pos - prevlen;
                if (prevlen <= sizeof(tmp) && currlen <= sizeof(tmp)) {
                    memcpy(tmp, l.buf + l.pos, currlen);
                    memmove(l.buf + prevstart + currlen,
                            l.buf + prevstart, prevlen);
                    memcpy(l.buf + prevstart, tmp, currlen);
                    if (l.pos + currlen <= l.len) l.pos += currlen;
                    refreshLine(&l);
                }
            }
            break;

        case CTRL_B:
            editorEditMoveLeft(&l);
            break;

        case CTRL_F:
            editorEditMoveRight(&l);
            break;

        case CTRL_P:
            editorEditHistoryNext(&l, EDITOR_HISTORY_PREV);
            break;

        case CTRL_N:
            editorEditHistoryNext(&l, EDITOR_HISTORY_NEXT);
            break;

        case ESC:   /* Escape sequence. */
            if (read(l.ifd, seq, 1) == -1) break;
            if (read(l.ifd, seq + 1, 1) == -1) break;

            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {
                    /* Extended escape: ESC [ <digit> ... <final> */
                    if (read(l.ifd, seq + 2, 1) == -1) break;
                    if (seq[2] == '~') {
                        switch (seq[1]) {
                        case '3': /* Delete key */
                            editorEditDelete(&l);
                            break;
                        }
                    }
                } else {
                    switch (seq[1]) {
                    case 'A': /* Up arrow */
                        editorEditHistoryNext(&l, EDITOR_HISTORY_PREV);
                        break;
                    case 'B': /* Down arrow */
                        editorEditHistoryNext(&l, EDITOR_HISTORY_NEXT);
                        break;
                    case 'C': /* Right arrow */
                        editorEditMoveRight(&l);
                        break;
                    case 'D': /* Left arrow */
                        editorEditMoveLeft(&l);
                        break;
                    case 'H': /* Home */
                        editorEditMoveHome(&l);
                        break;
                    case 'F': /* End */
                        editorEditMoveEnd(&l);
                        break;
                    }
                }
            } else if (seq[0] == 'O') {
                switch (seq[1]) {
                case 'H': /* Home */
                    editorEditMoveHome(&l);
                    break;
                case 'F': /* End */
                    editorEditMoveEnd(&l);
                    break;
                }
            }
            break;

        case CTRL_U:   /* Delete the whole line. */
            l.buf[0] = '\0';
            l.pos = l.len = 0;
            refreshLine(&l);
            break;

        case CTRL_K:   /* Delete from cursor to end. */
            l.buf[l.pos] = '\0';
            l.len = l.pos;
            refreshLine(&l);
            break;

        case CTRL_A:
            editorEditMoveHome(&l);
            break;

        case CTRL_E:
            editorEditMoveEnd(&l);
            break;

        case CTRL_L:
            editor_clear_screen();
            refreshLine(&l);
            break;

        case CTRL_W:
            editorEditDeletePrevWord(&l);
            break;

        case TAB:
            /* Tab is intentionally ignored (no completion support). */
            break;

        default:
            /* Regular character, possibly multi-byte UTF-8. Read the
             * remaining bytes of the sequence before inserting. */
            {
                char utf8[4];
                int utf8len = utf8ByteLen(c);
                utf8[0] = c;
                if (utf8len > 1) {
                    for (int i = 1; i < utf8len; i++) {
                        if (read(l.ifd, utf8 + i, 1) != 1) break;
                    }
                }
                editorEditInsert(&l, utf8, utf8len);
            }
            break;
        }
    }
}

/* ========================== History ======================================= */

/* Free all history entries. Called from atexit(). */
static void freeHistory(void) {
    if (history) {
        for (int j = 0; j < history_len; j++)
            free(history[j]);
        free(history);
    }
}

/* Restore terminal and free history on exit. */
static void editorAtExit(void) {
    disableRawMode(STDIN_FILENO);
    freeHistory();
}

/* Add a new entry to the history ring buffer.
 * Duplicate consecutive entries are silently dropped. */
int editor_history_add(const char *line) {
    char *linecopy;

    if (history_max_len == 0) return 0;

    /* Lazy initialization. */
    if (history == NULL) {
        history = malloc(sizeof(char *) * history_max_len);
        if (history == NULL) return 0;
        memset(history, 0, sizeof(char *) * history_max_len);
    }

    /* Don't add consecutive duplicates. */
    if (history_len && !strcmp(history[history_len - 1], line)) return 0;

    linecopy = strdup(line);
    if (!linecopy) return 0;

    /* Evict the oldest entry if we've reached the maximum. */
    if (history_len == history_max_len) {
        free(history[0]);
        memmove(history, history + 1, sizeof(char *) * (history_max_len - 1));
        history_len--;
    }
    history[history_len] = linecopy;
    history_len++;
    return 1;
}

/* Set the maximum number of history entries. If the current history
 * is larger, the oldest entries are dropped. */
void editor_history_set_max_len(int len) {
    char **new;

    if (len < 1) return;

    if (history) {
        int tocopy = history_len;

        new = malloc(sizeof(char *) * len);
        if (new == NULL) return;

        if (len < tocopy) {
            for (int j = 0; j < tocopy - len; j++)
                free(history[j]);
            tocopy = len;
        }
        memset(new, 0, sizeof(char *) * len);
        memcpy(new, history + (history_len - tocopy), sizeof(char *) * tocopy);
        free(history);
        history = new;
    }
    history_max_len = len;
    if (history_len > history_max_len)
        history_len = history_max_len;
}

/* ========================== Public API ==================================== */

/* Main entry point. Reads one line of input with interactive editing.
 * Returns a malloc'd string the caller must free, or NULL on
 * EOF / Ctrl-C / Ctrl-D / error (check errno to distinguish). */
char *editor_readline(const char *prompt) {
    char buf[EDITOR_MAX_LINE];

    if (!isatty(STDIN_FILENO)) {
        /* Input is a pipe or file — read without editing. */
        return editorNoTTY();
    }

    if (isUnsupportedTerm()) {
        /* Dumb terminal — just print the prompt and read a line. */
        char *retval;
        size_t len;

        printf("%s", prompt);
        fflush(stdout);
        retval = editorNoTTY();
        if (retval == NULL) return NULL;
        len = strlen(retval);
        while (len && retval[len - 1] == '\r') {
            len--;
            retval[len] = '\0';
        }
        return retval;
    }

    return editorEdit(STDIN_FILENO, STDOUT_FILENO, buf,
                      EDITOR_MAX_LINE, prompt);
}
