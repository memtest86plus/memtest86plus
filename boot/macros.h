// SPDX-License-Identifier: GPL-2.0
#ifndef MACROS_H
#define MACROS_H
/**
 * \file
 *
 * Provides miscellaneous useful definitions.
 *
 *//*
 * Copyright (C) 2024 Lionel Debroux.
 */

#ifndef __ASSEMBLY__

#ifdef __GNUC__
// Enhanced definitions under GCC and compatible, e.g. Clang.

// These are from GPLv2 Linux 6.7, for erroring out when the argument isn't an array type.
#define BUILD_BUG_ON_ZERO(e) ((int)(sizeof(struct { int:(-!!(e)); })))
#define __same_type(a, b)    __builtin_types_compatible_p(typeof(a), typeof(b))
#define __must_be_array(a)   BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))
#define ARRAY_SIZE(arr)      (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))
#else
// Fallback definitions.
#define ARRAY_SIZE(var_) (sizeof(var_) / sizeof((var_)[0]))
#endif

#endif

#endif
