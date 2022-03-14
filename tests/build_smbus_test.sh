#!/bin/sh
clang -O3 -g3 -Wall -W -fsanitize=address,undefined -DTESTING_MAIN ../system/smbus.c -o smbus_test -I ../boot
