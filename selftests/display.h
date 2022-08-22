// SPDX-License-Identifier: GPL-2.0

#define prints memtest_prints_mock
#define printf memtest_printf_mock

int printf(int row, int col, const char *fmt, ...);
int prints(int row, int col, const char *str);
