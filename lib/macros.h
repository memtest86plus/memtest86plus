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

#define min(x, y) ({                        \
    typeof(x) _min1 = (x);                  \
    typeof(y) _min2 = (y);                  \
    (void) (&_min1 == &_min2);              \
    _min1 < _min2 ? _min1 : _min2; })

#define max(x, y) ({                        \
    typeof(x) _max1 = (x);                  \
    typeof(y) _max2 = (y);                  \
    (void) (&_max1 == &_max2);              \
    _max1 > _max2 ? _max1 : _max2; })
#else
// Fallback definitions.
#define ARRAY_SIZE(var_) (sizeof(var_) / sizeof((var_)[0]))
#define min(x, y) (x < y ? x : y)
#define max(x, y) (x > y ? x : y)
#endif

#endif

#endif
