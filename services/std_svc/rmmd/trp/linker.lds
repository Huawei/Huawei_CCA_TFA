/*
 * (C) COPYRIGHT 2021 Arm Limited or its affiliates.
 * ALL RIGHTS RESERVED
 */

#include <common/bl_common.ld.h>
#include <lib/xlat_tables/xlat_tables_defs.h>

/* Mapped using 4K pages, requires us to align different sections with
 * different property at the same granularity. */
PAGE_SIZE_4K = 4096;

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(trp_head)

MEMORY {
	RAM (rwx): ORIGIN = RMM_BASE, LENGTH = RMM_LIMIT - RMM_BASE
}


SECTIONS
{
	. = RMM_BASE;

	.text : {
		__TEXT_START__ = .;
		*(.head.text)
		. = ALIGN(8);
		*(.text*)
		*(.vectors)
		. = ALIGN(PAGE_SIZE_4K);
		__TEXT_END__ = .;
	} >RAM

	. = ALIGN(PAGE_SIZE_4K);

	.rodata : {
		__RODATA_START__ = .;
		*(.rodata*)
		. = ALIGN(PAGE_SIZE_4K);
		__RODATA_END__ = .;
	} >RAM

	. = ALIGN(PAGE_SIZE_4K);

	 __RW_START__ = . ;

	.data : {
		*(.data*)
	} >RAM

	.bss (NOLOAD) : {
		__BSS_START__ = .;
		*(.bss*)
		__BSS_END__ = .;
	} >RAM
	__BSS_SIZE__ = SIZEOF(.bss);


	STACK_SECTION >RAM


	/*
	* Define a linker symbol to mark the end of the RW memory area for this
	* image.
	*/
	__RW_END__ = .;
	__RMM_END__ = .;


	/DISCARD/ : { *(.dynstr*) }
	/DISCARD/ : { *(.dynamic*) }
	/DISCARD/ : { *(.plt*) }
	/DISCARD/ : { *(.interp*) }
	/DISCARD/ : { *(.gnu*) }
	/DISCARD/ : { *(.note*) }
}
