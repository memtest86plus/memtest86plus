// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Martin Whitaker.

#include "ctype.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int toupper(int c)
{
    if (c >= 'a' && c <= 'z') {
        return c + 'A' -'a';
    } else {
        return c;
    }
}

int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int isxdigit(int c)
{
    return isdigit(c) || (toupper(c) >= 'A' && toupper(c) <= 'F');
}
