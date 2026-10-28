// SPDX-License-Identifier: GPL-2.0
#ifndef SPLASH_H
#define SPLASH_H
/**
 * \file
 *
 * Provides the graphical splash screen shown during initialisation when a
 * native graphics mode has been selected by the UEFI boot code.
 *
 *//*
 * Copyright (C) 2026 Sam Demeulemeester.
 */

#if defined(SPLASH_SCREEN)

/**
 * Shows the splash screen if in native graphics mode.
 */
void splash_start(void);

/**
 * Updates the status line (no effect if the splash screen is not shown).
 */
void splash_status(const char *message);

/**
 * Clears the status line and shows a message near the bottom (no effect if not shown).
 */
void splash_notice(const char *message);

/**
 * Removes the splash screen and restores the text display (no effect if not shown).
 */
void splash_end(void);

#else

static inline void splash_start(void) {}
static inline void splash_status(const char *message) { (void)message; }
static inline void splash_notice(const char *message) { (void)message; }
static inline void splash_end(void) {}

#endif

#endif // SPLASH_H
