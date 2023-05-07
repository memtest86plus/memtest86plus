// SPDX-License-Identifier: GPL-2.0
#ifndef AMD_SMN_H
#define AMD_SMN_H
/**
 * \file
 *
 * Provides various addresses and offsets on AMD's System Management Network
 *
 *//*
 * Copyright (C) 2023 Sam Demeulemeester.
 */

#define SMN_SMUIO_THM               0x00059800
#define SMN_THM_TCON_CUR_TMP        (SMN_SMUIO_THM + 0x00)

#endif // AMD_SMN_H