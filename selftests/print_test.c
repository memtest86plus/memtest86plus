// SPDX-License-Identifier: GPL-2.0

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "selftests.h"

int memtest_printf(int row, int col, const char *fmt, ...);

#define test_print(row, col, fmt, arg, exp) \
({                                                                  \
    char *result;                                                   \
    memtest_print_mock_init();                                      \
    memtest_printf(row, col, fmt, arg);                             \
    result = memtest_print_getresult();                             \
    if (strcmp(exp, result)) {                                      \
        fprintf(stderr, "Expected \"%s\", got \"%s\" %s:%s:%d\n",   \
                exp, result, __FILE__, __FUNCTION__, __LINE__);     \
    }                                                               \
})

int main() {

    test_print(1, 0, "test", NULL, "test");
    test_print(1, 1, "test %%", NULL, "test %");

    test_print(2, 2, "test %c", 'c', "test c");
    test_print(2, 3, "test %s", "str", "test str");

    test_print(3, 4, "%k", 1, "1K");
    test_print(3, 5, "%k", 1024, "1M");
    test_print(3, 6, "%k", 1024*1024, "1G");
    test_print(3, 7, "%k", 1024*1024*1024, "1T");
    test_print(3, 8, "%Sk", 1024*1024*1024, "1 T");

    test_print(4, 9, "%i", -123, "-123");

    test_print(5, 10, "%u", -1, "4294967295");

    test_print(6, 11, "%x", 0x31337abc, "31337abc");

    test_print(7, 12, "%03i", 6, "006");
    test_print(7, 13, "%3i", 6, "  6");
    test_print(7, 14, "%-3i", 6, "6  ");

    printf("All tests for %s passed.\n", __FILE__);

    return 0;
}
