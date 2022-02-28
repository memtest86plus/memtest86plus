// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022 Martin Whitaker.

#include <stdbool.h>

#include "boot.h"

#include "cpulocal.h"

//------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------

int local_bytes_used = 0;

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int allocate_local_flag(void)
{
    if (local_bytes_used == LOCALS_SIZE) {
        return -1;
    }
    return local_bytes_used += sizeof(bool);
}
