/*
 * Copyright (c) 2021, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <common/bl_common.h>
#include <common/debug.h>
#include <drivers/arm/pl011.h>
#include <drivers/console.h>
#include <plat/arm/common/plat_arm.h>
#include <platform_def.h>

/*******************************************************************************
 * Initialize the UART
 ******************************************************************************/
static console_t arm_trp_runtime_console;

void arm_trp_early_platform_setup(void)
{
	/*
	 * Initialize a different console than already in use to display
	 * messages from trp
	 */
	int rc = console_pl011_register(PLAT_ARM_TRP_UART_BASE,
					PLAT_ARM_TRP_UART_CLK_IN_HZ,
					ARM_CONSOLE_BAUDRATE,
					&arm_trp_runtime_console);
	if (rc == 0) {
		panic();
	}

	console_set_scope(&arm_trp_runtime_console,
			  CONSOLE_FLAG_BOOT | CONSOLE_FLAG_RUNTIME);
}

void arm_trp_plat_arch_setup(void)
{
	INFO("TRP: set up plat arch\n");

	const mmap_region_t bl_regions[] = {
		ARM_MAP_RMM_DRAM,
		ARM_MAP_RMM_CODE,
		ARM_MAP_RMM_RODATA,
		{0}
	};

	setup_page_tables(bl_regions, plat_arm_get_mmap());

	/* TRP runs in EL2 when RME enabled. */
	enable_mmu_el2(0);

	int ret = mmap_add_dynamic_region(0x80000000, 0x80000000, 0x3f000000,
			MT_MEMORY|MT_RW|MT_REALM);
	if (ret) {
		ERROR("arm_trp_plat_arch_setup: failed to set up dynamic region: %d\n", ret);
	}
	ret = mmap_add_dynamic_region(0xbf000000, 0xbf000000, 0x1000000,
			MT_MEMORY|MT_RW|MT_REALM);
	if (ret) {
		ERROR("arm_trp_plat_arch_setup: failed to set up dynamic region: %d\n", ret);
	}
}

void trp_early_platform_setup(void)
{
	arm_trp_early_platform_setup();
}

void trp_plat_arch_setup(void)
{
	arm_trp_plat_arch_setup();
}

void trp_plat_arch_enable_mmu(int id)
{
	if (!(read_sctlr_el2() & SCTLR_M_BIT)) {
		enable_mmu_el2(0);
	}
}
