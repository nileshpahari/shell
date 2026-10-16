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
#include "../include/history.h"

#define EDITOR_MAX_LINE 4096

struct editorState {
    int ifd;
    int ofd;
    char *buf;
    size_t buflen;
    const char *prompt;
    size_t plen;
    const char *rprompt;
    size_t rplen;
    size_t pos;
    size_t len;
    size_t cols;
    int history_index;
    int history_entry_added;
    int in_completion;
    size_t completion_idx;
};

struct abuf {
    char *b;
    int len;
};

static struct termios orig_termios;
static int rawmode = 0;
static int atexit_registered = 0;
static char *unsupported_term[] = {"dumb", "cons25", "emacs", NULL};

static editorCompletionCallback *completionCallback = NULL;

enum KEY_ACTION {
    KEY_NULL  = 0,
    CTRL_A    = 1,
    CTRL_B    = 2,
    CTRL_C    = 3,
    CTRL_D    = 4,
    CTRL_E    = 5,
    CTRL_F    = 6,
    CTRL_H    = 8,
    TAB       = 9,
    CTRL_K    = 11,
    CTRL_L    = 12,
    ENTER     = 13,
    CTRL_N    = 14,
    CTRL_P    = 16,
    CTRL_T    = 20,
    CTRL_U    = 21,
    CTRL_W    = 23,
    ESC       = 27,
    BACKSPACE = 127
};

static void editorAtExit(void);
static void refreshLine(struct editorState *l);

static int utf8ByteLen(char c) {
    unsigned char uc = (unsigned char)c;
    if ((uc & 0x80) == 0)    return 1;
    if ((uc & 0xE0) == 0xC0) return 2;
    if ((uc & 0xF0) == 0xE0) return 3;
    if ((uc & 0xF8) == 0xF0) return 4;
    return 1;
}

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
    return *p;
}

static size_t edNextCharLen(const char *buf, size_t pos, size_t len) {
    if (pos >= len) return 0;
    return (size_t)utf8ByteLen(buf[pos]);
}

static size_t edPrevCharLen(const char *buf, size_t pos) {
    if (pos == 0) return 0;
    size_t i = pos - 1;
    while (i > 0 && ((unsigned char)buf[i] & 0xC0) == 0x80)
        i--;
    return pos - i;
}

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

static int utf8CharWidth(uint32_t cp) {

    if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;

    if ((cp >= 0x0300 && cp <= 0x036F) ||
        (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) ||
        (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE20 && cp <= 0xFE2F))
        return 0;

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

static size_t utf8StrWidth(const char *s, size_t len) {
    size_t width = 0;
    size_t i = 0;

    while (i < len) {
        size_t clen;
        uint32_t cp = utf8DecodeChar(s + i, &clen);

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

static int isUnsupportedTerm(void) {
    char *term = getenv("TERM");
    if (term == NULL) return 0;
    for (int j = 0; unsupported_term[j]; j++)
        if (!strcasecmp(term, unsupported_term[j])) return 1;
    return 0;
}

static int enableRawMode(int fd) {
    struct termios raw;

    if (!isatty(STDIN_FILENO)) goto fatal;
    if (!atexit_registered) {
        atexit(editorAtExit);
        atexit_registered = 1;
    }
    if (tcgetattr(fd, &orig_termios) == -1) goto fatal;

    raw = orig_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    raw.c_oflag &= ~(OPOST);

    raw.c_cflag |= (CS8);

    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSAFLUSH, &raw) < 0) goto fatal;
    rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

static void disableRawMode(int fd) {
    if (rawmode && tcsetattr(fd, TCSAFLUSH, &orig_termios) != -1)
        rawmode = 0;
}

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

static int getColumns(int ifd, int ofd) {
    struct winsize ws;

    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        int start, cols;

        start = getCursorPosition(ifd, ofd);
        if (start == -1) goto failed;

        if (write(ofd, "\x1b[999C", 6) != 6) goto failed;
        cols = getCursorPosition(ifd, ofd);
        if (cols == -1) goto failed;

        if (cols > start) {
            char seq[32];
            snprintf(seq, 32, "\x1b[%dD", cols - start);
            if (write(ofd, seq, strlen(seq)) == -1) {              }
        }
        return cols;
    } else {
        return ws.ws_col;
    }

failed:
    return 80;
}

void editor_clear_screen(void) {
    if (write(STDOUT_FILENO, "\x1b[H\x1b[2J", 7) <= 0) {              }
}

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

static void editorBeep(void) {
    fprintf(stderr, "\x7");
    fflush(stderr);
}

static void freeCompletions(editorCompletions *lc) {
    size_t i;
    for (i = 0; i < lc->len; i++)
        free(lc->cvec[i]);
    if (lc->cvec != NULL)
        free(lc->cvec);
}

static void refreshLineWithCompletion(struct editorState *ls,
                                      editorCompletions *lc) {
    if (ls->completion_idx < lc->len) {
        struct editorState saved = *ls;
        ls->len = ls->pos = strlen(lc->cvec[ls->completion_idx]);
        ls->buf = lc->cvec[ls->completion_idx];
        refreshLine(ls);
        ls->len = saved.len;
        ls->pos = saved.pos;
        ls->buf = saved.buf;
    } else {
        refreshLine(ls);
    }
}

static int completeLine(struct editorState *ls, int keypressed) {
    editorCompletions lc = { NULL, 0 };
    int nwritten;
    char c = keypressed;

    completionCallback(ls->buf, &lc);
    if (lc.len == 0) {
        editorBeep();
        ls->in_completion = 0;
        c = 0;
    } else {
        switch (c) {
        case 9:
            if (ls->in_completion == 0) {
                ls->in_completion = 1;
                ls->completion_idx = 0;
            } else {
                ls->completion_idx = (ls->completion_idx + 1) % (lc.len + 1);
                if (ls->completion_idx == lc.len) editorBeep();
            }
            c = 0;
            break;
        case 27:
            if (ls->completion_idx < lc.len) refreshLine(ls);
            ls->in_completion = 0;
            c = 0;
            break;
        default:

            if (ls->completion_idx < lc.len) {
                nwritten = snprintf(ls->buf, ls->buflen, "%s",
                    lc.cvec[ls->completion_idx]);
                ls->len = ls->pos = nwritten;
            }
            ls->in_completion = 0;
            break;
        }

        if (ls->in_completion && ls->completion_idx < lc.len) {
            refreshLineWithCompletion(ls, &lc);
        } else {
            refreshLine(ls);
        }
    }

    freeCompletions(&lc);
    return c;
}

void editor_set_completion_callback(editorCompletionCallback *fn) {
    completionCallback = fn;
}

void editor_add_completion(editorCompletions *lc, const char *str) {
    size_t len = strlen(str);
    char *copy, **cvec;

    copy = malloc(len + 1);
    if (copy == NULL) return;
    memcpy(copy, str, len + 1);
    cvec = realloc(lc->cvec, sizeof(char *) * (lc->len + 1));
    if (cvec == NULL) {
        free(copy);
        return;
    }
    lc->cvec = cvec;
    lc->cvec[lc->len++] = copy;
}

static void refreshLine(struct editorState *l) {
    char seq[64];

    size_t pwidth = utf8StrWidth(l->rprompt, l->rplen);
    int fd = l->ofd;
    char *buf = l->buf;
    size_t len = l->len;
    size_t pos = l->pos;
    size_t poscol;
    size_t lencol;
    struct abuf ab;

    poscol = utf8StrWidth(buf, pos);
    lencol = utf8StrWidth(buf, len);

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

    while (pwidth + lencol > l->cols) {
        size_t clen = edPrevCharLen(buf, len);
        size_t dummy;
        uint32_t cp = utf8DecodeChar(buf + len - clen, &dummy);
        int cwidth = utf8CharWidth(cp);
        len -= clen;
        lencol -= cwidth;
    }

    abInit(&ab);

    abAppend(&ab, "\r", 1);

    abAppend(&ab, l->rprompt, l->rplen);
    abAppend(&ab, buf, len);

    snprintf(seq, sizeof(seq), "\x1b[0K");
    abAppend(&ab, seq, strlen(seq));

    snprintf(seq, sizeof(seq), "\r\x1b[%dC", (int)(poscol + pwidth));
    abAppend(&ab, seq, strlen(seq));

    if (write(fd, ab.b, ab.len) == -1) {                      }
    abFree(&ab);
}

static int editorEditInsert(struct editorState *l, const char *c, size_t clen) {
    if (l->len + clen >= l->buflen) return -1;

    if (l->len == l->pos) {

        memcpy(l->buf + l->pos, c, clen);
        l->pos += clen;
        l->len += clen;
        l->buf[l->len] = '\0';

        size_t bufwidth = utf8StrWidth(l->buf, l->len);
        if (utf8StrWidth(l->rprompt, l->rplen) + bufwidth < l->cols) {
            if (write(l->ofd, c, clen) == -1) return -1;
            return 0;
        }
    } else {

        memmove(l->buf + l->pos + clen, l->buf + l->pos, l->len - l->pos);
        memcpy(l->buf + l->pos, c, clen);
        l->pos += clen;
        l->len += clen;
        l->buf[l->len] = '\0';
    }
    refreshLine(l);
    return 0;
}

static void editorEditMoveLeft(struct editorState *l) {
    if (l->pos > 0) {
        l->pos -= edPrevCharLen(l->buf, l->pos);
        refreshLine(l);
    }
}

static void editorEditMoveRight(struct editorState *l) {
    if (l->pos != l->len) {
        l->pos += edNextCharLen(l->buf, l->pos, l->len);
        refreshLine(l);
    }
}

static void editorEditMoveHome(struct editorState *l) {
    if (l->pos != 0) {
        l->pos = 0;
        refreshLine(l);
    }
}

static void editorEditMoveEnd(struct editorState *l) {
    if (l->pos != l->len) {
        l->pos = l->len;
        refreshLine(l);
    }
}

static void editorEditHistoryNext(struct editorState *l, int dir) {
    const char *src = history_move(l->buf, &l->history_index, dir);
    if (src) {
        size_t slen = strlen(src);
        if (slen >= l->buflen) slen = l->buflen - 1;
        memcpy(l->buf, src, slen);
        l->buf[slen] = '\0';
        l->len = l->pos = slen;
        refreshLine(l);
    }
}

static void editorRemoveTemporaryHistory(struct editorState *l) {
    if (l->history_entry_added) {
        history_remove_last();
        l->history_entry_added = 0;
    }
}

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

static void editorEditDeletePrevWord(struct editorState *l) {
    size_t old_pos = l->pos;
    size_t diff;

    while (l->pos > 0 && l->buf[l->pos - 1] == ' ')
        l->pos -= edPrevCharLen(l->buf, l->pos);

    while (l->pos > 0 && l->buf[l->pos - 1] != ' ')
        l->pos -= edPrevCharLen(l->buf, l->pos);

    diff = old_pos - l->pos;
    memmove(l->buf + l->pos, l->buf + old_pos, l->len - old_pos + 1);
    l->len -= diff;
    refreshLine(l);
}

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

static char *editorNoTTY(void) {
    return editorReadLine(stdin);
}

static char *editorEdit(int stdin_fd, int stdout_fd, char *buf,
                        size_t buflen, const char *prompt) {
    struct editorState l;

    l.ifd = stdin_fd;
    l.ofd = stdout_fd;
    l.buf = buf;
    l.buflen = buflen - 1;
    l.prompt = prompt;
    l.plen = strlen(prompt);

    const char *last_nl = strrchr(prompt, '\n');
    l.rprompt = last_nl ? last_nl + 1 : prompt;
    l.rplen = strlen(l.rprompt);

    l.pos = 0;
    l.len = 0;
    l.cols = getColumns(stdin_fd, stdout_fd);
    l.history_index = 0;
    l.history_entry_added = 0;
    l.in_completion = 0;
    l.completion_idx = 0;
    l.buf[0] = '\0';

    if (enableRawMode(l.ifd) == -1) return NULL;

    l.history_entry_added = history_add("");

    if (write(l.ofd, prompt, l.plen) == -1) {
        editorRemoveTemporaryHistory(&l);
        disableRawMode(l.ifd);
        return NULL;
    }

    while (1) {
        char c;
        int nread;
        char seq[3];

        nread = read(l.ifd, &c, 1);
        if (nread <= 0) {

            editorRemoveTemporaryHistory(&l);
            disableRawMode(l.ifd);
            printf("\n");
            return NULL;
        }

        if ((l.in_completion || c == TAB) && completionCallback != NULL) {
            int retval = completeLine(&l, c);
            if (retval == 0) continue;
            c = retval;
        }

        switch (c) {
        case ENTER:
            editorRemoveTemporaryHistory(&l);
            disableRawMode(l.ifd);
            printf("\n");
            return strdup(l.buf);

        case CTRL_C:
            editorRemoveTemporaryHistory(&l);
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
                editorRemoveTemporaryHistory(&l);
                disableRawMode(l.ifd);
                printf("\n");
                errno = ENOENT;
                return NULL;
            }
            break;

        case CTRL_T:
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
            editorEditHistoryNext(&l, HISTORY_PREVIOUS);
            break;

        case CTRL_N:
            editorEditHistoryNext(&l, HISTORY_NEXT);
            break;

        case ESC:
            if (read(l.ifd, seq, 1) == -1) break;
            if (read(l.ifd, seq + 1, 1) == -1) break;

            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {

                    if (read(l.ifd, seq + 2, 1) == -1) break;
                    if (seq[2] == '~') {
                        switch (seq[1]) {
                        case '3':
                            editorEditDelete(&l);
                            break;
                        }
                    }
                } else {
                    switch (seq[1]) {
                    case 'A':
                        editorEditHistoryNext(&l, HISTORY_PREVIOUS);
                        break;
                    case 'B':
                        editorEditHistoryNext(&l, HISTORY_NEXT);
                        break;
                    case 'C':
                        editorEditMoveRight(&l);
                        break;
                    case 'D':
                        editorEditMoveLeft(&l);
                        break;
                    case 'H':
                        editorEditMoveHome(&l);
                        break;
                    case 'F':
                        editorEditMoveEnd(&l);
                        break;
                    }
                }
            } else if (seq[0] == 'O') {
                switch (seq[1]) {
                case 'H':
                    editorEditMoveHome(&l);
                    break;
                case 'F':
                    editorEditMoveEnd(&l);
                    break;
                }
            }
            break;

        case CTRL_U:
            l.buf[0] = '\0';
            l.pos = l.len = 0;
            refreshLine(&l);
            break;

        case CTRL_K:
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

            break;

        default:

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

static void editorAtExit(void) {
    disableRawMode(STDIN_FILENO);
    history_free();
}

char *editor_readline(const char *prompt) {
    char buf[EDITOR_MAX_LINE];

    if (!isatty(STDIN_FILENO)) {

        return editorNoTTY();
    }

    if (isUnsupportedTerm()) {

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
