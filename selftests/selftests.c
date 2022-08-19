// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "screen.h"

int memtest_vprintf(int row, int col, const char *fmt, va_list args);

struct memtest_print_expectation {
    int row;
    int col;
    char *str;
    struct memtest_print_expectation *next;
};

struct memtest_print_expectation *mpe = NULL;
char screen_line[SCREEN_WIDTH + 1];
int x_min, x_max, y_curr;

void memtest_print_mock_init() {
    x_min = x_max = y_curr = -1;
    memset(screen_line, 0, sizeof(screen_line));
}

char *memtest_print_getresult() {
    return &screen_line[x_min];
}

void print_char_mock(int row, int col, char ch)
{
    if (row < 0 || row >= SCREEN_HEIGHT ||
        col < 0 || col >= SCREEN_WIDTH)  {
        fprintf(stderr, "Write outside the screen: [%d, %d]: '%c'\n",
                    row, col, ch);
        exit(1);
    }

    if (y_curr == -1) {
        y_curr = row;
    } else if (y_curr != row) {
        fprintf(stderr, "Write to wrong row: %d !=%d\n", y_curr, row);
        exit(1);
    }

    if (x_min == -1 || col < x_min) x_min = col;
    if (x_max == -1 || col > x_max) x_max = col;

    screen_line[col] = ch;
}

void memtest_print_add_expectation(int row, int col, const char *str) {

    struct memtest_print_expectation *e;

    e = calloc(1, sizeof(*e));
    if (!e) {
        fprintf(stderr, "calloc failed: %d", errno);
    }

    e->row = row;
    e->col = col;
    e->str = strdup(str);
    e->next = mpe;
    mpe = e;
}

void memtest_print_done() {
    if (mpe) {
        fprintf(stderr, "Unhandled print expectation: printf(%d, %d, \"%s\").\n",
                    mpe->row, mpe->col, mpe->str);
        exit(1);
    }
}

int memtest_printf_mock(int row, int col, const char *fmt, ...) {
    va_list args;
    struct memtest_print_expectation *ex;
    int rv;
    char *result;

    memtest_print_mock_init();

    va_start(args, fmt);
    rv = memtest_vprintf(row, col, fmt, args);
    va_end(args);

    result = memtest_print_getresult();

    if (!mpe) {
        fprintf(stderr, "Nothing expected but called printf(%d, %d, \"%s\").\n",
                    row, col, result);
        exit(1);
    }

    ex = mpe;

    if (ex->row != row
         || ex->col != col
         || strcmp(ex->str, result)) {

        fprintf(stderr, "Expected printf(%d, %d, \"%s\") but called printf(%d, %d, \"%s\").\n",
                    ex->row, ex->col, ex->str,
                    row, col, result);
        exit(1);
    }

    mpe = mpe->next;

    free(ex->str);
    free(ex);

    return rv;
}

int memtest_prints_mock(int row, int col, const char *str) {
    return memtest_printf_mock(row, col, "%s", str);
}

void do_check_int(const char *file, const char *function, int line,
                  const char *check_name, int a, int b) {

    if (a == b) return;

    printf("%s: %d != %d in %s:%s:%d\n",
            check_name, a, b, file, function, line);

    exit(1);
}

void do_check_str(const char *file, const char *function, int line,
                  const char *check_name, const char *a, const char *b) {

    if (!strcmp(a, b)) return;

    printf("%s: \"%s\" != \"%s\" in %s:%s:%d\n",
            check_name, a, b, file, function, line);

    exit(1);
}
