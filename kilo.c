/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(key) ((key) & 0x1f)

/*** data ***/

struct editorConfig
{
    int cursor_x, cursor_y;
    int screenrows;
    int screencols;
    struct termios original_termios;
};

struct editorConfig Editor;

/*** terminal ***/

void die(const char *errmsg)
{
    write(STDIN_FILENO, "\x1b[2J", 4);
    write(STDIN_FILENO, "\x1b[H", 3);

    perror(errmsg);
    exit(1);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &Editor.original_termios) == -1) {
        die("tcsetattr");
    }
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &Editor.original_termios) == -1) {
        die("tcgetattr");
    }
    atexit(disableRawMode);

    struct termios raw = Editor.original_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

char editorReadKey()
{
    int nread;
    char ch;
    while ((nread = read(STDIN_FILENO, &ch, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN) die ("read");
    }
    return ch;
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while(i < sizeof(buf) - 1)
    {
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** append buffer ***/

typedef struct appendBuffer
{
    char *buffer;
    int length;
} abuf;

#define ABUF_INIT {NULL, 0}

void abAppend(abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->buffer, ab->length + len);

    if(new == NULL) return;
    memcpy(&new[ab->length], s, len);
    ab->buffer = new;
    ab->length += len;
}

void abFree(abuf *ab)
{
    free(ab->buffer);
}

/*** output ***/

void editorDrawRows(abuf *ab)
{
    int y;
    for (y = 0; y < Editor.screenrows; y++)
    {
        if(y == Editor.screenrows / 3)
        {
            char welcome[80];
            int welcome_len = snprintf(welcome, sizeof(welcome),
                                       "Kilo Editor -- version %s", KILO_VERSION);
            if(welcome_len > Editor.screencols) welcome_len = Editor.screencols;

            int centering = (Editor.screencols - welcome_len) / 2;
            if (centering)
            {
                abAppend(ab, "#", 1);
                centering--;
            }
            while (centering--) abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcome_len);
        }
        else
        {
            abAppend(ab, "#", 1);
        }

        abAppend(ab, "\x1b[K", 3);

        if(y < Editor.screenrows - 1)
        {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen()
{
    abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", Editor.cursor_y + 1, Editor.cursor_x + 1);
    abAppend(&ab, buffer, strlen(buffer));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.buffer, ab.length);
    abFree(&ab);
}

/*** input ***/

void editorMoveCursor(char key)
{
    switch(key)
    {
    case 'a':
        Editor.cursor_x--;
        break;
    case 'd':
        Editor.cursor_x++;
        break;
    case 'w':
        Editor.cursor_y--;
        break;
    case 's':
        Editor.cursor_y++;
        break;
    }
}

void editorProcessKeypress()
{
    char ch = editorReadKey();

    switch (ch)
    {
    case CTRL_KEY('q'):
        write(STDIN_FILENO, "\x1b[2J", 4);
        write(STDIN_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case 'w':
    case 'a':
    case 's':
    case 'd':
        editorMoveCursor(ch);
        break;
    }
}

/*** init and main ***/


void initEditor()
{
    Editor.cursor_x = 0;
    Editor.cursor_y = 0;

    if(getWindowSize(&Editor.screenrows, &Editor.screencols) == -1) die("getWindowSize");
}

int main()
{
    enableRawMode();
    initEditor();

    for(;;)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
