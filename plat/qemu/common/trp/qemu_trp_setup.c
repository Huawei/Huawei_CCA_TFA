#include <assert.h>
#include <platform_def.h>
#include <drivers/console.h>
#include <drivers/arm/pl011.h>
#include <plat/common/platform.h>
#include <lib/xlat_tables/xlat_tables_v2.h>

#include "../qemu_private.h"

static console_t qemu_trp_runtime_console;

void qemu_trp_early_platform_setup(void)
{
	/*TODO trp should use another UART other than the BOOT UART */
	(void)console_pl011_register(PLAT_QEMU_BOOT_UART_BASE,
			PLAT_QEMU_BOOT_UART_CLK_IN_HZ,
			PLAT_QEMU_CONSOLE_BAUDRATE, &qemu_trp_runtime_console);

	console_set_scope(&qemu_trp_runtime_console, CONSOLE_FLAG_BOOT |
			CONSOLE_FLAG_RUNTIME);
}

void qemu_trp_plat_arch_setup(void)
{
	qemu_configure_mmu_el2(RMM_BASE, RMM_SIZE,
					RMM_CODE_BASE, RMM_CODE_LIMIT,
					RMM_RO_DATA_BASE, RMM_RO_DATA_LIMIT,
					RMM_COHERENT_BASE, RMM_COHERENT_LIMIT);
	int ret = mmap_add_dynamic_region(0x40000000, 0x40000000, 0xc0000000, MT_MEMORY|MT_RW|MT_REALM);
	if (ret) {
		ERROR("arm_trp_plat_arch_setup: setting up dynamic region 0x40000000: %d\n", ret);
	}
	ret = mmap_add_dynamic_region(0x40000000, 0x140000000, 0xc0000000, MT_MEMORY|MT_RW|MT_NS);
	if (ret) {
		ERROR("arm_trp_plat_arch_setup: setting up shadow dynamic region 0x40000000: %d\n", ret);
		panic();
	}
}

void trp_early_platform_setup(void)
{
	qemu_trp_early_platform_setup();
}

void trp_plat_arch_setup(void)
{
	qemu_trp_plat_arch_setup();
}

void trp_plat_arch_enable_mmu(int id)
{
	if (!(read_sctlr_el2() & SCTLR_M_BIT)) {
		enable_mmu_el2(0);
	}
}
