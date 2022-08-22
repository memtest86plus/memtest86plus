// SPDX-License-Identifier: GPL-2.0

#include "screen.h"

extern char *screen_line;

#define check_int(v, e) do_check_int(__FILE__, __FUNCTION__, __LINE__, #v, v, e)
#define check_str(v, e) do_check_str(__FILE__, __FUNCTION__, __LINE__, #v, v, e)

void do_check_int(const char *file, const char *function, int line,
                  const char *check_name, int a, int b);

void do_check_str(const char *file, const char *function, int line,
                  const char *check_name, const char *a, const char *b);

void memtest_print_mock_init();
char *memtest_print_getresult();
void memtest_print_add_expectation(int row, int col, const char *str);
void memtest_print_done();
