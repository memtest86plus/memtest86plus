CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wshadow -O3 -D_DEFAULT_SOURCE -g
INC_DIRS = -I. -I../system

all: print_test smbus_test spd-decode

all_tests_run: print_test_run smbus_test_run

print.o: print_wrapper.c ../lib/print.c
	$(CC) -c $(CFLAGS) -o $@ $< $(INC_DIRS) -I../lib -fno-builtin

smbus.o: smbus_wrapper.c ../system/smbus.c
	$(CC) -c $(CFLAGS) -o $@ $< $(INC_DIRS) -fno-builtin

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $< $(INC_DIRS)

print_test: print_test.o print.o selftests.o
smbus_test: smbus_test.o smbus.o print.o selftests.o
spd-decode: spd-decode.o smbus.o print.o

print_test_run: print_test
	./print_test

smbus_test_run: smbus_test
	./smbus_test

clean:
	rm -rf *.o print_test smbus_test spd-decode
