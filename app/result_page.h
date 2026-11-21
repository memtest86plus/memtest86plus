// SPDX-License-Identifier: GPL-2.0
#ifndef RESULT_PAGE_H
#define RESULT_PAGE_H
/**
 * \file
 *
 * Provides the graphical result page, which shows the memory slots and the
 * modules the errors were attributed to. Only available in native graphics
 * mode.
 *
 *//*
 * Copyright (C) 2026 Sam Demeulemeester.
 */

#include <stdbool.h>

#if defined(SPLASH_SCREEN)

/**
 * Returns true if the result page can be shown: native graphics mode, and the
 * display is not needed for the trace or the serial console.
 */
bool result_page_available(void);

/**
 * Shows the result page. Returns false if it is not available.
 */
bool result_page_show(void);

/**
 * Returns true if the result page is shown.
 */
bool result_page_shown(void);

/**
 * Redraws the result page if it is shown and the pass or error count changed.
 */
void result_page_refresh(void);

/**
 * Removes the result page and restores the text display (no effect if not shown).
 */
void result_page_hide(void);

#else

static inline bool result_page_available(void) { return false; }
static inline bool result_page_show(void) { return false; }
static inline bool result_page_shown(void) { return false; }
static inline void result_page_refresh(void) {}
static inline void result_page_hide(void) {}

#endif

#endif // RESULT_PAGE_H
