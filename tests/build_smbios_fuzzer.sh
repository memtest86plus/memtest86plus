#!/bin/sh
clang -O3 -g3 -Wall -W -fsanitize=fuzzer,address,undefined -DFUZZING -DFUZZING_LIBFUZZER ../system/smbios.c -o smbios_fuzz -I ../boot
