/*
 * Copyright (c) 2021-2022, Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TRP_PRIVATE_H
#define TRP_PRIVATE_H

#define ARM_EXCEPTION_IRQ   0
#define ARM_EXCEPTION_FIQ   1
#define ARM_EXCEPTION_EL1_SERROR    2
#define ARM_EXCEPTION_TRAP  3

/* Definitions to help the assembler access the SMC/ERET args structure */
#define TRP_ARGS_SIZE		TRP_ARGS_END
#define TRP_ARG0		0x0
#define TRP_ARG1		0x8
#define TRP_ARG2		0x10
#define TRP_ARG3		0x18
#define TRP_ARG4		0x20
#define TRP_ARG5		0x28
#define TRP_ARG6		0x30
#define TRP_ARG7		0x38
#define TRP_ARGS_END		0x40

#ifndef __ASSEMBLER__

#include <stdint.h>
#include <assert.h>
#include <lib/xlat_tables/xlat_tables_v2.h>
#include <lib/el3_runtime/aarch64/context.h>
#include "vgic-v3.h"

#define rmm_err(fmt, ...) \
	ERROR("rmm : " fmt, ## __VA_ARGS__)

#define rmm_info(fmt, ...) \
	INFO("rmm : " fmt, ## __VA_ARGS__)

#define rmm_notice(fmt, ...) \
	NOTICE("rmm : " fmt, ## __VA_ARGS__)

#define rmm_warn(fmt, ...) \
	WARN("rmm : " fmt, ## __VA_ARGS__)

#define rmm_verbose(fmt, ...) \
	VERBOSE("rmm : " fmt, ## __VA_ARGS__)


// TODO reference them from platform. using 4GB for now
#define RMM_NUM_GRANULES2 20
#define RMM_NUM_GRANULES (1UL << RMM_NUM_GRANULES2)
#define RMM_GRANULE_SIZE2 12
#define RMM_GRANULE_SIZE (1UL << RMM_GRANULE_SIZE2)

#define RMM_RTT_BLOCK_LEVEL 2
#define RMM_RTT_PAGE_LEVEL 3

typedef uint64_t rmm_addr_t;

/* Data structure to hold SMC arguments */
typedef struct trp_args {
	uint64_t regs[TRP_ARGS_END >> 3];
} __aligned(CACHE_WRITEBACK_GRANULE) trp_args_t;

#define write_trp_arg(args, offset, val) (((args)->regs[offset >> 3])	\
					 = val)

typedef enum rmm_granule_state {
	GS_UNDELEGATED, GS_DELEGATED, GS_DATA, GS_RD, GS_REC, GS_REC_AUX, GS_RTT
} rmm_granule_state_e;

typedef enum rmm_physical_address_space {
	PAS_NS, PAS_REALM, PAS_ROOT, PAS_SECURE
} rmm_physical_address_space_e;

typedef enum rmm_realm_state {
	RS_ACTIVE, RS_NEW, RS_SYSTEM_OFF
} rmm_realm_state_e;

typedef enum rmm_rtt_entry_state {
	ES_ASSIGNED, ES_DESTROYED, ES_TABLE, ES_UNASSIGNED, ES_VALID, ES_VALID_NS
} rmm_rtt_entry_state_e;

typedef struct rmm_system_registers {
	el1_sysregs_t el1regs;
	uint64_t hcr_el2; // in
	uint64_t spsr_el2; // in & out
	uint64_t vtcr_el2; // in
	uint64_t vttbr_el2; // in
	uint64_t vmpidr_el2; // in
	uint64_t esr_el2; // out
	uint64_t far_el2; // out
	uint64_t hpfar_el2; // out
	vgic_v3_cpu_if_t cpu_if;
} rmm_system_registers_t;

typedef struct rmm_realm {
	//TODO measurement;
	//TODO measurement_algo;
	uint64_t par_base;
	uint64_t par_size;
	uint64_t rec_index;
	uint64_t rec_count; // number of RECs currently owned. Checked when destroying realm
	uint64_t rtt_base;
	uint64_t rtt_num_start;
	unsigned int ipa_width;
	unsigned int vmid;
	int rtt_level_start;
	rmm_realm_state_e state;
} rmm_realm_t;

#define REC_ENTER_REASON_FIRST_RUN	0
#define REC_ENTER_REASON_IRQ		1
#define REC_ENTER_REASON_FIQ		2
#define REC_ENTER_REASON_EL1_SERROR	3
#define REC_ENTER_REASON_TRAP		4

typedef struct rmm_rec {
	uint64_t gprs[32];
	rmm_system_registers_t sysregs;
	rmm_realm_t *owner_rd;
	uint64_t dispose_base;
	uint64_t dispose_size;
	uint64_t pc;
	uint64_t attest_addr;
	uint64_t attest_challenge[8];
	uint64_t aux[16];
	uint64_t enter_reason;
	uint64_t psci_complete_result;
	bool runnable;
	bool emulatable_abort;
	bool psci_pending;
	bool state_running;
	bool attest_in_progress;
} rmm_rec_t;

/* RMI error codes. */
#define RMI_SUCCESS              0
#define RMI_ERROR_INPUT          1
#define RMI_ERROR_MEMORY         2
#define RMI_ERROR_ALIAS          3
#define RMI_ERROR_IN_USE         4
#define RMI_ERROR_REALM_STATE    5
#define RMI_ERROR_OWNER          6
#define RMI_ERROR_REC            7
#define RMI_ERROR_RTT_WALK       8
#define RMI_ERROR_RTT_ENTRY      9
#define RMI_ERROR_NOT_SUPPORTED 10
#define RMI_ERROR_INTERNAL      11

/* RMI handled by TRP */
#define RMI_FNUM_VERSION_REQ		U(0x150)

#define RMI_FNUM_GRANULE_DELEGATE	U(0x151)
#define RMI_FNUM_GRANULE_UNDELEGATE	U(0x152)
#define RMI_FNUM_DATA_CREATE           U(0x153)
#define RMI_FNUM_DATA_CREATE_UNKNOWN   U(0x154)
#define RMI_FNUM_DATA_DESTROY          U(0x155)
#define RMI_FNUM_DATA_DISPOSE          U(0x156)
#define RMI_FNUM_REALM_ACTIVATE        U(0x157)
#define RMI_FNUM_REALM_CREATE          U(0x158)
#define RMI_FNUM_REALM_DESTROY         U(0x159)
#define RMI_FNUM_REC_CREATE            U(0x15A)
#define RMI_FNUM_REC_DESTROY           U(0x15B)
#define RMI_FNUM_REC_ENTER             U(0x15C)
#define RMI_FNUM_RTT_CREATE            U(0x15D)
#define RMI_FNUM_RTT_DESTROY           U(0x15E)
#define RMI_FNUM_RTT_MAP_UNPROTECTED   U(0x15F)
#define RMI_FNUM_RTT_MAP_PROTECTED     U(0x160)
#define RMI_FNUM_RTT_READ_ENTRY        U(0x161)
#define RMI_FNUM_RTT_UNMAP_UNPROTECTED U(0x162)
#define RMI_FNUM_RTT_UNMAP_PROTECTED   U(0x163)
#define RMI_FNUM_PSCI_COMPLETE         U(0x164)
#define RMI_FNUM_FEATURES              U(0x165)
#define RMI_FNUM_RTT_FOLD              U(0x166)
#define RMI_FNUM_REC_AUX_COUNT         U(0x167)
#define RMI_FNUM_DATA_CREATE_LEVEL     U(0x168)
#define RMI_FNUM_DATA_CREATE_UNKNOWN_LEVEL U(0x169)
#define RMI_FNUM_DATA_DESTROY_LEVEL    U(0x16A)

#define RMI_RMM_REQ_VERSION		        RMM_FID(SMC_64, RMI_FNUM_VERSION_REQ)
#define RMI_RMM_GRANULE_DELEGATE	    RMM_FID(SMC_64, RMI_FNUM_GRANULE_DELEGATE)
#define RMI_RMM_GRANULE_UNDELEGATE	    RMM_FID(SMC_64, RMI_FNUM_GRANULE_UNDELEGATE)
#define RMI_RMM_DATA_CREATE             RMM_FID(SMC_64, RMI_FNUM_DATA_CREATE)
#define RMI_RMM_DATA_CREATE_UNKNOWN     RMM_FID(SMC_64, RMI_FNUM_DATA_CREATE_UNKNOWN)
#define RMI_RMM_DATA_DESTROY            RMM_FID(SMC_64, RMI_FNUM_DATA_DESTROY)
#define RMI_RMM_DATA_DISPOSE            RMM_FID(SMC_64, RMI_FNUM_DATA_DISPOSE)
#define RMI_RMM_REALM_ACTIVATE          RMM_FID(SMC_64, RMI_FNUM_REALM_ACTIVATE)
#define RMI_RMM_REALM_CREATE            RMM_FID(SMC_64, RMI_FNUM_REALM_CREATE)
#define RMI_RMM_REALM_DESTROY           RMM_FID(SMC_64, RMI_FNUM_REALM_DESTROY)
#define RMI_RMM_REC_CREATE              RMM_FID(SMC_64, RMI_FNUM_REC_CREATE)
#define RMI_RMM_REC_DESTROY             RMM_FID(SMC_64, RMI_FNUM_REC_DESTROY)
#define RMI_RMM_REC_ENTER               RMM_FID(SMC_64, RMI_FNUM_REC_ENTER)
#define RMI_RMM_RTT_CREATE              RMM_FID(SMC_64, RMI_FNUM_RTT_CREATE)
#define RMI_RMM_RTT_DESTROY             RMM_FID(SMC_64, RMI_FNUM_RTT_DESTROY)
#define RMI_RMM_RTT_MAP_UNPROTECTED     RMM_FID(SMC_64, RMI_FNUM_RTT_MAP_UNPROTECTED)
#define RMI_RMM_RTT_MAP_PROTECTED       RMM_FID(SMC_64, RMI_FNUM_RTT_MAP_PROTECTED)
#define RMI_RMM_RTT_READ_ENTRY          RMM_FID(SMC_64, RMI_FNUM_RTT_READ_ENTRY)
#define RMI_RMM_RTT_UNMAP_UNPROTECTED   RMM_FID(SMC_64, RMI_FNUM_RTT_UNMAP_UNPROTECTED)
#define RMI_RMM_RTT_UNMAP_PROTECTED     RMM_FID(SMC_64, RMI_FNUM_RTT_UNMAP_PROTECTED)
#define RMI_RMM_PSCI_COMPLETE           RMM_FID(SMC_64, RMI_FNUM_PSCI_COMPLETE)
#define RMI_RMM_FEATURES                RMM_FID(SMC_64, RMI_FNUM_FEATURES)
#define RMI_RMM_RTT_FOLD                RMM_FID(SMC_64, RMI_FNUM_RTT_FOLD)
#define RMI_RMM_REC_AUX_COUNT           RMM_FID(SMC_64, RMI_FNUM_REC_AUX_COUNT)
#define RMI_RMM_DATA_CREATE_LEVEL       RMM_FID(SMC_64, RMI_FNUM_DATA_CREATE_LEVEL)
#define RMI_RMM_DATA_CREATE_UNKNOWN_LEVEL RMM_FID(SMC_64, RMI_FNUM_DATA_CREATE_UNKNOWN_LEVEL)
#define RMI_RMM_DATA_DESTROY_LEVEL      RMM_FID(SMC_64, RMI_FNUM_DATA_DESTROY_LEVEL)

/* Definitions for RMI VERSION */
#define RMI_ABI_VERSION_MAJOR		U(0x0)
#define RMI_ABI_VERSION_MINOR		U(0x0)
#define RMI_ABI_VERSION			((RMI_ABI_VERSION_MAJOR << 16) | \
					RMI_ABI_VERSION_MINOR)

/* an address is delegable iff it falls within the range of GPT and
 * granule_state_table */
static inline bool rmm_is_addr_delegable(uint64_t addr) {
	return addr < (RMM_NUM_GRANULES << RMM_GRANULE_SIZE2);
}
static inline bool rmm_is_addr_granule_aligned(uint64_t addr) {
	return (addr & 0xfff) == 0;
}
static inline bool rmm_is_addr_delegable_galigned(uint64_t addr) {
	uint64_t mask = ~((RMM_NUM_GRANULES - 1) << RMM_GRANULE_SIZE2);
	return (addr & mask) == 0;
}

/* The function rmm_is_addr_rtt_level_aligned() is used to evaluate whether
 * an address is aligned to the address range described by an RTTE at a
 * specified RTT level. */
static inline bool rmm_is_addr_rtt_level_aligned(uint64_t addr, int level) {
	uint64_t mask = (1 << (12 + 9 * (3 - level))) - 1;
	return (addr & mask) == 0;
}

void rmm_granule_state_table_init (void);
rmm_granule_state_e rmm_get_granule_state (uint64_t addr);
void rmm_set_granule_state (uint64_t addr, rmm_granule_state_e new_state);
static inline bool rmm_assert_granule_state (uint64_t addr, rmm_granule_state_e exp_state) {
	return rmm_is_addr_delegable_galigned(addr) && rmm_get_granule_state(addr) == exp_state;
}
static inline bool rmm_assert_granule_state_level (uint64_t addr, int level, rmm_granule_state_e exp_state) {
	if (!rmm_is_addr_rtt_level_aligned(addr, level))
		return false;
	const uint64_t size = RMM_GRANULE_SIZE << (9 * (3-level));
	for (uint64_t offset = 0; offset < size; offset += RMM_GRANULE_SIZE) {
		if (!rmm_assert_granule_state(addr + offset, exp_state))
			return false;
	}
	return true;
}

/* Current RTT implementation assume 48-bit output address. I.e. bit[47:12] is OA.
 * Bit 56 in block and page descriptors flags DESTROYED status. 0:UNASSIGNED; 1:DESTROYED
 * Bit 55 in block and page descriptors flags NS/Realm. 0:Realm; 1:NS */
#define RTT_OABIT_MIN 12
#define RTT_OABIT_MAX 48 // well, it's max+1
#define RTT_OA_MASK (((1UL << (RTT_OABIT_MAX - RTT_OABIT_MIN)) - 1) << RTT_OABIT_MIN)
#define RTT_DESTROYED_BIT 56
#define RTT_NS_BIT 55

typedef struct rmm_rtt_walk_result {
	uint64_t rtt_addr; /* The address of the RTT granule */
	uint64_t *rtte; /* The address of the RTT entry. So rtt_addr <= rtte < rtt_addr+4K */
	int level; /* Level of the RTT. rd.rtt_level_start <= level <= 3 */
} rmm_rtt_walk_result_t;

rmm_rtt_entry_state_e rmm_get_rtte_state (const rmm_rtt_walk_result_t *walk_result);
uint64_t rmm_get_rtte_outaddr (const rmm_rtt_walk_result_t *walk_result);
void rmm_set_rtte (rmm_rtt_walk_result_t *walk_result, uint64_t out_addr, bool valid_bit);
void rmm_set_rtte_table (rmm_rtt_walk_result_t *walk_result, uint64_t child_rtt);
/* change valid bit without affecting out addr, used by MAP_PROT and UNMAP_PROT */
void rmm_set_rtte_valid (rmm_rtt_walk_result_t *walk_result, bool is_valid);
/* soly used by RMI_DATA_DESTROY */
void rmm_set_rtte_destroyed (rmm_rtt_walk_result_t *walk_result);
/* soly used by RMI_MAP_UNPROTECTED */
void rmm_set_rtte_ns (rmm_rtt_walk_result_t *walk_result, uint64_t ns_rtte);
void rmm_rtt_walk (rmm_rtt_walk_result_t *result, const rmm_realm_t *rd, uint64_t target_addr, int target_level);
void rmm_rtt_walk_close (rmm_rtt_walk_result_t *result);
bool rmm_rtt_fold(rmm_rtt_walk_result_t *parent_rtte);

static inline void rmm_remap_granule(uint64_t addr, unsigned int attr)
{
	rmm_remap_xlat_table_entry(addr, attr);
}

static inline void rmm_undo_remap_granule(uint64_t addr)
{
	rmm_undo_remap_xlat_table_entry(addr);
}

/* copy <size> bytes of memory from <ns_src> to <rmm_dst>
 * <rmm_dst> must point to realm memory. I.e. RMM stack/heap, DELEGATED, RD, DATA...
 * <ns_src> must point to non-secure memory.
 * return true if copy is successful.
 * return false if copy is failed, because for example ns_src points to ROOT. */
bool rmm_read_ns(void *rmm_dst, const void *ns_src, size_t size);
bool rmm_write_ns (void *ns_dst, const void *rmm_src, size_t size);

/* Helper to issue SMC calls to BL31 */
uint64_t trp_smc(trp_args_t *);

uint64_t __realm_enter(rmm_rec_t *prec);

/* The main function to executed only by Primary CPU */
void trp_main(void);

/* Setup TRP. Executed only by Primary CPU */
void trp_setup(void);

#endif /* __ASSEMBLER__ */
#endif /* TRP_PRIVATE_H */
