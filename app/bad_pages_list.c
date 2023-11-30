#include <stdbool.h>
#include <stdint.h>

#include "display.h"

#include "bad_pages_list.h"
#include "test.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAX_PAGES 70

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static uintptr_t    pages[MAX_PAGES];
static int          num_pages = 0;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

/*
 * Check if pages already contains page
 */
static bool pages_contains(testword_t page)
{
    for (int i = 0; i < num_pages; i++) {
        if (pages[i] == page) {
            return true;
        }
    }
    return false;
}

/*
 * Insert page at index idx, shuffling other entries towards the end.
 */
static void insert_at(testword_t page, int idx)
{
    // Move all entries >= idx one index towards the end to make space for the new entry
    for (int i = num_pages - 1; i >= idx; i--) {
        pages[i + 1] = pages[i];
    }

    pages[idx] = page;
    num_pages++;
}

/*
 * Insert page into pages array at an index i so that pages[i-1] < pages[i]
 * NOTE: Assumes pages is already sorted and has space
 */
static void insert_sorted(testword_t page)
{
    // Find index to insert entry into
    int new_idx = num_pages;
    for (int i = 0; i < num_pages; i++) {
        if (page < pages[i]) {
            new_idx = i;
            break;
        }
    }

    insert_at(page, new_idx);
}

/*
 * 
 */
static int hex_string_length(uintptr_t value) {
    int length = 0;
    while (value != 0) {
        length += 1;
        value /= 0x10;
    }
    return length;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void bad_pages_list_init(void)
{
    num_pages = 0;

    for (int idx = 0; idx < MAX_PAGES; idx++) {
        pages[idx] = 0u;
    }
}

bool bad_pages_list_insert(testword_t page)
{
    // If covered by existing entry we return immediately
    if (pages_contains(page) || (num_pages + 1) > MAX_PAGES) {
        return false;
    }

    insert_sorted(page);
    return true;
}

void bad_pages_list_display(void)
{
    if (num_pages == 0) {
        return;
    }

    check_input();

    clear_message_area();
    scroll_message_row -= 1;

    int col = 0;
    for (int i = 0; i < num_pages; i++) {
        if (i > 0) {
            display_scrolled_message(col, " ");
            col++;
        }

        int hex_width = hex_string_length(pages[i]);
        if (hex_width < 2) {
            hex_width = 2;
        }

        int text_width = 2 + hex_width;
        if (col + text_width > SCREEN_WIDTH) {
            scroll();
            col = 0;
        }

        display_scrolled_message(col, "0x%02x", pages[i]);
        col += text_width;
    }
}
