// SPDX-License-Identifier: GPL-2.0
//
// SBAT metadata for the .sbat PE section, included by the per-arch header.S.
// The vendor version tracks MT_VERSION; bump the component generation
// (2nd field) on security-relevant releases (see shim's SBAT.md).

	.ascii	"sbat,1,SBAT Version,sbat,1,https://github.com/rhboot/shim/blob/main/SBAT.md\n"
	.ascii	"memtest86+,1,Memtest86+,mt86plus," , MT_VERSION , ",https://github.com/memtest86plus\n"
