
/*
 * Copyright (c) 2015-2022, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <platform_def.h>

#include <arch_helpers.h>
#include <common/bl_common.h>
#include <lib/xlat_tables/xlat_tables_v2.h>
#include <services/el3_spmc_ffa_memory.h>

#include <plat/common/platform.h>
#include "qemu_private.h"

#define MAP_DEVICE0	MAP_REGION_FLAT(DEVICE0_BASE,			\
					DEVICE0_SIZE,			\
					MT_DEVICE | MT_RW | MT_SECURE)

#ifdef DEVICE1_BASE
#define MAP_DEVICE1	MAP_REGION_FLAT(DEVICE1_BASE,			\
					DEVICE1_SIZE,			\
					MT_DEVICE | MT_RW | MT_SECURE)
#endif

#ifdef DEVICE2_BASE
#define MAP_DEVICE2	MAP_REGION_FLAT(DEVICE2_BASE,			\
					DEVICE2_SIZE,			\
					MT_DEVICE | MT_RW | MT_SECURE)
#endif

#define MAP_SHARED_RAM	MAP_REGION_FLAT(SHARED_RAM_BASE,		\
					SHARED_RAM_SIZE,		\
					MT_DEVICE  | MT_RW | MT_SECURE)

#define MAP_BL32_MEM	MAP_REGION_FLAT(BL32_MEM_BASE, BL32_MEM_SIZE,	\
					MT_MEMORY | MT_RW | MT_SECURE)

#define MAP_NS_DRAM0	MAP_REGION_FLAT(NS_DRAM0_BASE, NS_DRAM0_SIZE,	\
					MT_MEMORY | MT_RW | MT_NS)

#define MAP_FLASH0	MAP_REGION_FLAT(QEMU_FLASH0_BASE, QEMU_FLASH0_SIZE, \
					MT_MEMORY | MT_RO | MT_SECURE)

#define MAP_FLASH1	MAP_REGION_FLAT(QEMU_FLASH1_BASE, QEMU_FLASH1_SIZE, \
					MT_MEMORY | MT_RO | MT_SECURE)

#if ENABLE_RME
#define MAP_GPT_L0	MAP_REGION_FLAT(QEMU_L0_GPT_ADDR_BASE, \
					QEMU_L0_GPT_SIZE, \
					MT_MEMORY | MT_RW | MT_ROOT)

#define MAP_GPT_L1	MAP_REGION_FLAT(QEMU_L1_GPT_ADDR_BASE, \
					QEMU_L1_GPT_SIZE, \
					MT_MEMORY | MT_RW | MT_ROOT)

#define MAP_REALM	MAP_REGION_FLAT(RMM_BASE, RMM_SIZE, \
					MT_MEMORY | MT_RW | MT_REALM)

#define MAP_RMM_SHARED	MAP_REGION_FLAT(RMM_SHARED_BASE, RMM_SHARED_SIZE, \
						MT_MEMORY | MT_RW | MT_REALM)
#endif


/*
 * Table of regions for various BL stages to map using the MMU.
 * This doesn't include TZRAM as the 'mem_layout' argument passed to
 * arm_configure_mmu_elx() will give the available subset of that,
 */
#ifdef IMAGE_BL1
static const mmap_region_t plat_qemu_mmap[] = {
	MAP_FLASH0,
	MAP_FLASH1,
	MAP_SHARED_RAM,
	MAP_DEVICE0,
#ifdef MAP_DEVICE1
	MAP_DEVICE1,
#endif
#ifdef MAP_DEVICE2
	MAP_DEVICE2,
#endif
	{0}
};
#define MT_IMAGE_PAS	MT_ROOT
#endif
#ifdef IMAGE_BL2
static const mmap_region_t plat_qemu_mmap[] = {
	MAP_FLASH0,
	MAP_FLASH1,
	MAP_SHARED_RAM,
#if ENABLE_RME
	MAP_GPT_L0,
	MAP_GPT_L1,
	MAP_REALM,
#endif
	MAP_DEVICE0,
#ifdef MAP_DEVICE1
	MAP_DEVICE1,
#endif
#ifdef MAP_DEVICE2
	MAP_DEVICE2,
#endif
	MAP_NS_DRAM0,
#if SPM_MM
	QEMU_SP_IMAGE_MMAP,
#else
	MAP_BL32_MEM,
#endif
	{0}
};
#define MT_IMAGE_PAS	MT_ROOT
#endif
#ifdef IMAGE_BL31
static const mmap_region_t plat_qemu_mmap[] = {
	MAP_SHARED_RAM,
#if ENABLE_RME
	MAP_GPT_L0,
	MAP_GPT_L1,
	MAP_RMM_SHARED,
	MAP_REALM,
#endif
	MAP_DEVICE0,
#ifdef MAP_DEVICE1
	MAP_DEVICE1,
#endif
#ifdef MAP_DEVICE2
	MAP_DEVICE2,
#endif
#if SPM_MM
	MAP_NS_DRAM0,
	QEMU_SPM_BUF_EL3_MMAP,
#elif !SPMC_AT_EL3
	MAP_BL32_MEM,
#endif
	{0}
};
#define MT_IMAGE_PAS	MT_ROOT
#endif
#ifdef IMAGE_BL32
static const mmap_region_t plat_qemu_mmap[] = {
	MAP_SHARED_RAM,
	MAP_DEVICE0,
#ifdef MAP_DEVICE1
	MAP_DEVICE1,
#endif
#ifdef MAP_DEVICE2
	MAP_DEVICE2,
#endif
	{0}
};
#endif
#ifdef IMAGE_RMM
static const mmap_region_t plat_qemu_mmap[] = {
	MAP_SHARED_RAM,
	MAP_DEVICE0,
#ifdef MAP_DEVICE1
	MAP_DEVICE1,
#endif
#ifdef MAP_DEVICE2
	MAP_DEVICE2,
#endif
	{0}
};
#define MT_IMAGE_PAS   MT_REALM
#endif

/*******************************************************************************
 * Returns QEMU platform specific memory map regions.
 ******************************************************************************/
const mmap_region_t *plat_qemu_get_mmap(void)
{
	return plat_qemu_mmap;
}

#if MEASURED_BOOT || TRUSTED_BOARD_BOOT
int plat_get_mbedtls_heap(void **heap_addr, size_t *heap_size)
{
	return get_mbedtls_heap_helper(heap_addr, heap_size);
}
#endif

#if SPMC_AT_EL3
/*
 * When using the EL3 SPMC implementation allocate the datastore
 * for tracking shared memory descriptors in normal memory.
 */
#define PLAT_SPMC_SHMEM_DATASTORE_SIZE 64 * 1024

uint8_t plat_spmc_shmem_datastore[PLAT_SPMC_SHMEM_DATASTORE_SIZE];

int plat_spmc_shmem_datastore_get(uint8_t **datastore, size_t *size)
{
	*datastore = plat_spmc_shmem_datastore;
	*size = PLAT_SPMC_SHMEM_DATASTORE_SIZE;
	return 0;
}

int plat_spmc_shmem_begin(struct ffa_mtd *desc)
{
	return 0;
}

int plat_spmc_shmem_reclaim(struct ffa_mtd *desc)
{
	return 0;
}
#endif

#if ENABLE_RME
/*
 * Get a pointer to the RMM-EL3 Shared buffer and return it
 * through the pointer passed as parameter.
 *
 * This function returns the size of the shared buffer.
 */
size_t plat_rmmd_get_el3_rmm_shared_mem(uintptr_t *shared)
{
	*shared = (uintptr_t)RMM_SHARED_BASE;

	return (size_t)RMM_SHARED_SIZE;
}

int plat_rmmd_load_manifest(struct rmm_manifest *manifest)
{
	uint64_t checksum;
	struct ns_dram_bank *bank_ptr;

	assert(manifest != NULL);

	manifest->version = RMMD_MANIFEST_VERSION;
	manifest->padding = 0U; /* RES0 */
	manifest->plat_data = (uintptr_t)NULL;
	manifest->plat_dram.num_banks = 1;

	/*
	 * Array ns_dram_banks[] follows ns_dram_info structure:
	 *
	 * +-----------------------------------+
	 * |  offset  |   field   |  comment   |
	 * +----------+-----------+------------+
	 * |    0     |  version  | 0x00000002 |
	 * +----------+-----------+------------+
	 * |    4     |  padding  | 0x00000000 |
	 * +----------+-----------+------------+
	 * |    8     | plat_data |    NULL    |
	 * +----------+-----------+------------+
	 * |    16    | num_banks |            |
	 * +----------+-----------+            |
	 * |    24    |   banks   | plat_dram  |
	 * +----------+-----------+            |
	 * |    32    | checksum  |            |
	 * +----------+-----------+------------+
	 * |    40    |  base 0   |            |
	 * +----------+-----------+   bank[0]  |
	 * |    48    |  size 0   |            |
	 * +----------+-----------+------------+
	 * |    56    |  base 1   |            |
	 * +----------+-----------+   bank[1]  |
	 * |    64    |  size 1   |            |
	 * +----------+-----------+------------+
	 */
	bank_ptr = (struct ns_dram_bank *)
			((uintptr_t)&manifest->plat_dram.checksum +
			sizeof(manifest->plat_dram.checksum));

	manifest->plat_dram.banks = bank_ptr;

	/* Calculate checksum of plat_dram structure */
	checksum = 1 + (uint64_t)bank_ptr;

	/* Store DRAM banks data in Boot Manifest */
	bank_ptr[0].base = NS_DRAM0_BASE;
	bank_ptr[0].size = NS_DRAM0_SIZE;

	/* Update checksum */
	checksum += bank_ptr[0].base + bank_ptr[0].size;

	/* Checksum must be 0 */
	manifest->plat_dram.checksum = ~checksum + 1UL;

	return 0;
}
#endif	/* ENABLE_RME */
