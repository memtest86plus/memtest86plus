// SPDX-License-Identifier: GPL-2.0
#ifndef SCREEN_H
#define SCREEN_H
/**
 * \file
 *
 * Provides the display interface. It provides an 80x25 VGA-compatible text
 * display.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdint.h>

/**
 * Screen size definitions. The screen size cannot be changed.
 */
#define SCREEN_WIDTH    80
#define SCREEN_HEIGHT   25

/**
 * Colours that can be used for the foreground or background.
 */
typedef enum {
    BLACK       = 0,
    BLUE        = 1,
    GREEN       = 2,
    CYAN        = 3,
    RED         = 4,
    MAUVE       = 5,
    YELLOW      = 6,
    WHITE       = 7
} screen_colour_t;

/**
 * Modifier that can be added to any foreground colour.
 * Has no effect on background colours.
 */
#define BOLD        8

/**
 * Initialise the display interface.
 */
void screen_init(void);

/**
 * Set the foreground colour used for subsequent drawing operations.
 */
void set_foreground_colour(screen_colour_t colour);

/**
 * Set the background colour used for subsequent drawing operations.
 */
void set_background_colour(screen_colour_t colour);

/**
 * Clear the whole screen, using the current background colour.
 */
void clear_screen(void);

/**
 * Clear the specified region of the screen, using the current background
 * colour.
 */
void clear_screen_region(int start_row, int start_col, int end_row, int end_col);

/**
 * Move the contents of the specified region of the screen up one row,
 * discarding the top row, and clearing the bottom row, using the current
 * background colour.
 */
void scroll_screen_region(int start_row, int start_col, int end_row, int end_col);

/**
 * Copy the contents of the specified region of the screen into the supplied
 * buffer.
 */
void save_screen_region(int start_row, int start_col, int end_row, int end_col, uint16_t buffer[]);

/**
 * Restore the specified region of the screen from the supplied buffer.
 * This restores both text and colours.
 */
void restore_screen_region(int start_row, int start_col, int end_row, int end_col, const uint16_t buffer[]);

/**
 * Write the supplied character to the specified screen location, using the
 * current foreground colour. Has no effect if the location is outside the
 * screen.
 */
void print_char(int row, int col, char ch);

#endif // SCREEN_H
