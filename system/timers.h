// SPDX-License-Identifier: GPL-2.0
#ifndef _TIMERS_H_
#define _TIMERS_H_
/**
 * \file
 *
 * Provides support for various timers sources
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 * Copyright (C) 2004-2022 Sam Demeulemeester
 */


/**
 * Initialize timers (to correct TSC frequency)
 */
void timers_init(void);

#endif /* _TIMERS_H_ */
