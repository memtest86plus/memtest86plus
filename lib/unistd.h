// SPDX-License-Identifier: GPL-2.0
#ifndef UNISTD_H
#define UNISTD_H
/**
 * \file
 *
 * Provides a subset of the functions normally provided by <unistd.h>.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

/**
 * Sleeps for at least usec microseconds.
 */
void usleep(unsigned int usec);

/**
 * Sleeps for at least sec seconds.
 */
void sleep(unsigned int sec);

#endif // UNISTD_H
