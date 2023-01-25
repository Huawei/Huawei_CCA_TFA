/*
 * Copyright (c) 2021-2022, Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch.h>
#include <inttypes.h>
#include <common/debug.h>
#include <plat/common/platform.h>
#include <services/rmmd_svc.h>
#include <services/trp/platform_trp.h>
#include <lib/xlat_tables/xlat_tables_v2.h>
#include <lib/spinlock.h>

#include <platform_def.h>
#include "trp_private.h"
#include "rmi.h"
#include "vgic-v3.h"
#include "time.h"

/*******************************************************************************
 * Per cpu data structure to populate parameters for an SMC in C code and use
 * a pointer to this structure in assembler code to populate x0-x7
 ******************************************************************************/
static trp_args_t trp_smc_args[PLATFORM_CORE_COUNT];
static spinlock_t rmm_lock;

static const unsigned int pa_range_bits_arr[] = {
	PARANGE_0000, PARANGE_0001, PARANGE_0010, PARANGE_0011, PARANGE_0100,
	PARANGE_0101, PARANGE_0110
};

/*******************************************************************************
 * Set the arguments for SMC call
 ******************************************************************************/
static trp_args_t *set_smc_args(uint64_t arg0,
				uint64_t arg1,
				uint64_t arg2,
				uint64_t arg3,
				uint64_t arg4,
				uint64_t arg5,
				uint64_t arg6,
				uint64_t arg7)
{
	uint32_t linear_id;
	trp_args_t *pcpu_smc_args;

	/*
	 * Return to Secure Monitor by raising an SMC. The results of the
	 * service are passed as an arguments to the SMC
	 */
	linear_id = plat_my_core_pos();
	pcpu_smc_args = &trp_smc_args[linear_id];
	write_trp_arg(pcpu_smc_args, TRP_ARG0, arg0);
	write_trp_arg(pcpu_smc_args, TRP_ARG1, arg1);
	write_trp_arg(pcpu_smc_args, TRP_ARG2, arg2);
	write_trp_arg(pcpu_smc_args, TRP_ARG3, arg3);
	write_trp_arg(pcpu_smc_args, TRP_ARG4, arg4);
	write_trp_arg(pcpu_smc_args, TRP_ARG5, arg5);
	write_trp_arg(pcpu_smc_args, TRP_ARG6, arg6);
	write_trp_arg(pcpu_smc_args, TRP_ARG7, arg7);

	return pcpu_smc_args;
}

/*******************************************************************************
 * Setup function for TRP.
 ******************************************************************************/
void trp_setup(void)
{
	/* Perform early platform-specific setup */
	trp_early_platform_setup();
	trp_plat_arch_setup();
}

/* Main function for TRP */
void trp_main(void)
{
	rmm_notice("TRP: %s\n", version_string);
	rmm_notice("TRP: %s\n", build_message);
	rmm_info("TRP: Memory base : 0x%lx\n", (unsigned long)RMM_BASE);
	rmm_info("TRP: Total size : 0x%lx bytes\n", (unsigned long)(RMM_END
			- RMM_BASE));
}

void trp_enable_mmu(void)
{
	int linear_id = plat_my_core_pos();
	trp_plat_arch_enable_mmu(linear_id);
}

/*******************************************************************************
 * Returning RMI version back to Normal World
 ******************************************************************************/
static trp_args_t *trp_ret_rmi_version(void)
{
	rmm_verbose("RMM version is %u.%u\n", RMI_ABI_VERSION_MAJOR,
					  RMI_ABI_VERSION_MINOR);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, RMI_ABI_VERSION,
			    0, 0, 0, 0, 0, 0);
}

/*******************************************************************************
 * Transitioning granule of NON-SECURE type to REALM type
 ******************************************************************************/
static trp_args_t *trp_asc_mark_realm(unsigned long long x1)
{
	unsigned long long ret;

	rmm_verbose("Delegating granule 0x%llx\n", x1);
	//TODO: how to check Granule(addr).pas != NS
	if (!rmm_assert_granule_state(x1, GS_UNDELEGATED)) {
		rmm_info("Delegating 0x%llx with state %d\n", x1, rmm_get_granule_state(x1));
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	ret = trp_smc(set_smc_args(RMMD_GTSI_DELEGATE, x1, 0, 0, 0, 0, 0, 0));

	if (ret != 0ULL) {
		ERROR("Granule transition from NON-SECURE type to REALM type "
		      "failed 0x%llx\n", ret);
	}
	rmm_set_granule_state(x1, GS_DELEGATED);
out:
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

/*******************************************************************************
 * Transitioning granule of REALM type to NON-SECURE type
 ******************************************************************************/
static trp_args_t *trp_asc_mark_nonsecure(unsigned long long x1)
{
	unsigned long long ret;

	rmm_verbose("Undelegating granule 0x%llx\n", x1);
	if (!rmm_assert_granule_state(x1, GS_DELEGATED)) {
		rmm_info("Undelegating 0x%llx with state %d\n", x1, rmm_get_granule_state(x1));
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	memset((void *)x1, 0, RMM_GRANULE_SIZE);

	ret = trp_smc(set_smc_args(RMMD_GTSI_UNDELEGATE, x1, 0, 0, 0, 0, 0, 0));

	if (ret != 0ULL) {
		rmm_err("Granule transition from REALM type to NON-SECURE type "
			"failed 0x%llx\n", ret);
	}
	rmm_set_granule_state(x1, GS_UNDELEGATED);
out:
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static bool rmm_is_feature_valid(int index, uint64_t feature)
{
	switch (index) {
	case 0:
		if (feature & 0x100) {
			/* do not support FEAT_LPA2 */
			return false;
		}
		uint8_t s2sz;
		uint64_t pa_range = read_id_aa64mmfr0_el1() &
				    ID_AA64MMFR0_EL1_PARANGE_MASK;
		s2sz = feature & 0xff;
		if (s2sz <= pa_range_bits_arr[pa_range]) {
			return true;
		}
		return false;
	default:
		assert(0);
	}

	return false;
}

static inline bool rmm_is_rtt_params_valid(int64_t rtt_level_start,
		uint64_t rtt_num_start,
		unsigned int ipa_width)
{
	/* FEAT_LPA2 is not implemented, yet, so no rtt_level_start = -1. */
	if (rtt_level_start < 0 ||
	    rtt_level_start > RMM_RTT_PAGE_LEVEL) {
		rmm_info("invalid rtt_level_start %d\n", (int)rtt_level_start);
		return false;
	}
	/* Level 3 RTT covers 21 bits, i.e. 2MB.
	 * Level 2 RTT covers 30 bits, i.e. 1GB.
	 * <root_bits> is how many LSB address bit does one starting level RTT cover. */
	int root_bits = 12 + (4 - rtt_level_start) * 9;
	if (ipa_width + 9 < root_bits) {
		rmm_info("rtt_level_start %d is too small for ipa_width %u. Don't need such a tall tree.\n",
			 (int)rtt_level_start, ipa_width);
		return false;
	}
	if (ipa_width > root_bits) { // need more than one starting level RTT
		if (rtt_num_start != (1 << (ipa_width - root_bits))) {
			rmm_info("rtt_num_start %llx should be %llx\n",
				 (unsigned long long)rtt_num_start,
				 1ULL << (ipa_width - root_bits));
			return false;
		}
	}
	return true;
}

#define VMID_LIMIT 256
static bool rmm_vmid_used[VMID_LIMIT];
static unsigned int rmm_last_vmid = 0;

/* return 0 if no VMID available */
static unsigned int get_vmid (void)
{
	for (int i = 0; i < VMID_LIMIT; i ++) {
		rmm_last_vmid = (rmm_last_vmid + 1) % VMID_LIMIT;
		if (rmm_last_vmid == 0) // vmid0 is reserved for RMM
			continue;
		if (!rmm_vmid_used[rmm_last_vmid]) {
			rmm_vmid_used[rmm_last_vmid] = true;
			return rmm_last_vmid;
		}
	}
	return 0;
}

static void put_vmid (unsigned int vmid)
{
	rmm_vmid_used[vmid] = false;
}

static trp_args_t *trp_create_realm (rmm_addr_t rd, rmm_addr_t params)
{
	unsigned long long ret;
	rmi_realm_params_t realm_params;
	unsigned int ipa_width;

	spin_lock(&rmm_lock);
	rmm_verbose("create realm 0x%" PRIx64 " 0x%" PRIx64 "\n", rd, params);

	if (!rmm_assert_granule_state(params, GS_UNDELEGATED)) {
		rmm_info("create realm param 0x%" PRIx64 " with state %d\n", params,
			 rmm_get_granule_state(params));
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (!rmm_assert_granule_state(rd, GS_DELEGATED)) {
		rmm_info("create realm rd 0x%" PRIx64 " with state %d\n", rd,
			 rmm_get_granule_state(rd));
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	/* copy the params from NS to realm */
	if (!rmm_read_ns(&realm_params, (void *)params, sizeof(realm_params))) {
		rmm_info("read NS realm params failed: rd 0x%" PRIx64 " params 0x%" PRIx64 "\n", rd, params);
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	if (!rmm_is_feature_valid(0, realm_params.features_0)) {
		ret = RMI_ERROR_MEMORY;
		goto out;
	}
	ipa_width = realm_params.features_0 & 0xff;

	if (!rmm_is_rtt_params_valid(realm_params.rtt_level_start,
				     realm_params.rtt_num_start, ipa_width)) {
		ret = RMI_ERROR_MEMORY;
		goto out;
	}

	if (realm_params.par_base >= 1UL << ipa_width ||
	    realm_params.par_size >= 1UL << ipa_width ||
	    realm_params.par_base + realm_params.par_size >= 1UL << ipa_width) {
		rmm_info("create realm ipa_width=%u, par_base=0x%" PRIx64 ", par_size=0x%" PRIx64 "\n",
			 ipa_width, realm_params.par_base,
			 realm_params.par_size);
		ret = RMI_ERROR_MEMORY;
		goto out;
	}

	for (int i = 0; i < realm_params.rtt_num_start; i++) {
		if (!rmm_assert_granule_state(realm_params.rtt_base + RMM_GRANULE_SIZE * i, GS_DELEGATED)) {
			rmm_info("create realm rtt_base 0x%" PRIx64 " with state %d\n",
				 realm_params.rtt_base + RMM_GRANULE_SIZE * i,
				 rmm_get_granule_state(realm_params.rtt_base + RMM_GRANULE_SIZE * i));
			ret = RMI_ERROR_MEMORY;
			goto out;
		}
	}

	unsigned int vmid = get_vmid();
	if (vmid == 0) {
		rmm_info("create realm no available VMID\n");
		ret = RMI_ERROR_INTERNAL;
		goto out;
	}

	rmm_realm_t *rdp = (rmm_realm_t *)rd;
	rdp->par_base = realm_params.par_base;
	rdp->par_size = realm_params.par_size;
	rdp->ipa_width = ipa_width;
	rdp->rec_index = 0;
	rdp->rec_count = 0;
	rdp->rtt_base = realm_params.rtt_base;
	rdp->rtt_level_start = realm_params.rtt_level_start;
	rdp->rtt_num_start = realm_params.rtt_num_start;
	rdp->vmid = vmid;
	rdp->state = RS_NEW;

	memset((void *)rdp->rtt_base, 0, RMM_GRANULE_SIZE * rdp->rtt_num_start);
	for (int i = 0; i < realm_params.rtt_num_start; i ++)
		rmm_set_granule_state(realm_params.rtt_base + RMM_GRANULE_SIZE * i, GS_RTT);
	rmm_set_granule_state(rd, GS_RD);
	ret = 0;
out:
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static inline bool is_rtte_ready_to_destroy (uint64_t rtte)
{
	if (rtte & 3UL) // TABLE
		return false;
	uint64_t outaddr = rtte & RTT_OA_MASK;
	if (!outaddr) // UNASSIGNED, DESTROYED
		return true;
	return rtte & (1UL << RTT_NS_BIT);
}

static trp_args_t *trp_destroy_realm (unsigned long long rd)
{
	unsigned long long ret;

	spin_lock(&rmm_lock);
	rmm_verbose("destroy realm 0x%llx\n", rd);

	if (!rmm_assert_granule_state(rd, GS_RD)) {
		rmm_info("destroy realm rd 0x%llx with state %d\n", rd, rmm_get_granule_state(rd));
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	const rmm_realm_t *prd = (const rmm_realm_t *)rd;

	if (prd->rec_count) {
		ret = RMI_ERROR_IN_USE;
		goto out;
	}

	/* Make sure all RTTE in all starting level RTT is one of
	 * UNASSIGNED, DESTROYED, VALID_NS.
	 * I.e. not pointing to DATA or RTT*/
	for (int i = 0; i < prd->rtt_num_start; i ++) {
		uint64_t rtt = prd->rtt_base + i * RMM_GRANULE_SIZE;
		if (!rmm_assert_granule_state(rtt, GS_RTT)) {
			rmm_err("destroy realm but starting rtt 0x%llx is %d\n",
				(unsigned long long)rtt, rmm_get_granule_state(rtt));
			ret = RMI_ERROR_INTERNAL;
			goto out;
		}
		for (int j = 0; j < RMM_GRANULE_SIZE / sizeof(uint64_t); j ++) {
			uint64_t rtte = ((uint64_t *)rtt)[j];
			if (!is_rtte_ready_to_destroy(rtte)) {
				rmm_info("destroy realm but RTTE 0x%llx not ready to go\n", (unsigned long long)rtte);
				ret = RMI_ERROR_IN_USE;
				goto out;
			}
		}
	}

	put_vmid(prd->vmid);
	for (int i = 0; i < prd->rtt_num_start; i ++) {
		uint64_t rtt = prd->rtt_base + i * RMM_GRANULE_SIZE;
		rmm_set_granule_state(rtt, GS_DELEGATED);
	}
	rmm_set_granule_state(rd, GS_DELEGATED);

	ret = 0;
out:
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static trp_args_t *trp_activate_realm (uint64_t rd)
{
	uint64_t ret;
	spin_lock(&rmm_lock);
	rmm_verbose("activate realm 0x%" PRIx64 "\n", rd);

	if (!rmm_assert_granule_state(rd, GS_RD)) {
		rmm_info("activate realm 0x%" PRIx64 " with state %d\n", rd,
			 rmm_get_granule_state(rd));
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	rmm_realm_t *prd = (rmm_realm_t *)rd;
	if (prd->state != RS_NEW) {
		rmm_info("activate realm rd.state=%d\n", prd->state);
		ret = RMI_ERROR_REALM_STATE;
		goto out;
	}

	prd->state = RS_ACTIVE;
	ret = RMI_SUCCESS;

out:
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static inline uint64_t mpidr2recindex (uint64_t mpidr)
{
	uint64_t aff3 = (mpidr >> 12UL) & 0x0ff00000;
	uint64_t aff12 = (mpidr >> 4UL) & 0x000ffff0;
	uint64_t aff0 = mpidr & 0xf;
	return aff3 | aff12 | aff0;
}

/* input: mpidr parameter of REC_CREATE
 * output: mpidr_el1 register */
static inline uint64_t mpidr2reg(uint64_t mpidr)
{
	const uint64_t U = 0;
	const uint64_t MT = 0;
	const uint64_t RES1 = 0x80000000UL;
	uint64_t aff3 = (mpidr >> 32UL) & 0xffUL;
	uint64_t aff012 = mpidr & 0x00ffffffUL;
	return aff3 | aff012 | RES1 | (U << 30UL) | (MT << 24UL);
}

static trp_args_t *trp_rec_create (uint64_t rec, uint64_t rd, uint64_t mpidr, uint64_t params)
{
	uint64_t ret;
	spin_lock(&rmm_lock);
	rmm_verbose("create REC 0x%" PRIx64 " 0x%" PRIx64 " 0x%" PRIx64 " 0x%" PRIx64 "\n",
		    rec, rd, mpidr, params);

	if (!rmm_assert_granule_state(params, GS_UNDELEGATED)) {
		rmm_info("create rec param 0x%" PRIx64 " with state %d\n", params,
			 rmm_get_granule_state(params));
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (!rmm_assert_granule_state(rd, GS_RD)) {
		rmm_info("create rec rd 0x%" PRIx64 " with state %d\n", rd,
			 rmm_get_granule_state(rd));
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (!rmm_assert_granule_state(rec, GS_DELEGATED)) {
		rmm_info("create rec rec 0x%" PRIx64 " with state %d\n", rec,
			 rmm_get_granule_state(rec));
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	rmm_realm_t *prd = (rmm_realm_t *)rd;
	if (prd->state != RS_NEW) {
		rmm_info("create rec rd.state=%d\n", prd->state);
		ret = RMI_ERROR_REALM_STATE;
		goto out;
	}
	if (mpidr2recindex(mpidr) != prd->rec_index) {
		rmm_info("create rec mpidr=0x%" PRIx64 ", rec_index=0x%" PRIx64 "\n",
			 mpidr, prd->rec_index);
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	rmi_rec_params_t rec_params;
	if (!rmm_read_ns(&rec_params, (void *)params, sizeof(rec_params))) {
		rmm_info("read NS rec params failed: params 0x%" PRIx64 "\n", params);
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	prd->rec_index ++;
	prd->rec_count ++;
	rmm_set_granule_state(rec, GS_REC);
	rmm_rec_t *prec = (rmm_rec_t *)rec;
	prec->owner_rd = prd;
	prec->state_running = false;
	prec->runnable = (rec_params.flags & 1) == 1; // TODO check RES0 bits?
	for (int i = 0; i < 8; i ++)
		prec->gprs[i] = rec_params.gprs[i];
	for (int i = 8; i < 32; i ++)
		prec->gprs[i] = 0;
	prec->pc = rec_params.pc;
	prec->dispose_size = 0;

	prec->enter_reason = REC_ENTER_REASON_FIRST_RUN;

	// Set sctlr_el1, spsr_el2, hcr_el2, vtcr_el2 based on current observation
	memset((void*)(&prec->sysregs), 0, sizeof(rmm_system_registers_t));

	write_ctx_reg(&(prec->sysregs.el1regs), CTX_SCTLR_EL1,
		      SCTLR_SA_BIT | SCTLR_SA0_BIT | SCTLR_CP15BEN_BIT | SCTLR_nAA_BIT
		      | SCTLR_NTWI_BIT | SCTLR_NTWE_BIT | SCTLR_EIS_BIT | SCTLR_SPAN_BIT);

	/* Set SPSR_EL2, set PSTATE.EL and PSTATE.SP on executing an exception
	 * return operation in EL2. At present we set PSTATE.EL = EL1,
	 * PSTATE.SP=SP_ELx, need to reconsider in future
	 */
	prec->sysregs.spsr_el2 = SPSR_64(MODE_EL1, MODE_SP_ELX, DAIF_DBG_BIT | DAIF_ABT_BIT | DAIF_IRQ_BIT | DAIF_FIQ_BIT);

	/* Set HCR_EL2
	 * HCR_FWB_BIT: Enable stage 2 FWB
	 * HCR_RW_BIT: Register width control for lower exception. RW=1: EL1 is AArch64
	 * HCR_TSC_BIT: Trap SMC instruction. TSC=1: SMC instruction executed in NS_EL1 is trapped to EL2
	 * HCR_BSU: Barrier shareability
	 * HCR_VM_BIT:  Enable second stage of translation. VM=1: Enable second stage translation
	 */
	prec->sysregs.hcr_el2 = HCR_VM_BIT| HCR_SWIO_BIT | HCR_PTW_BIT |
				HCR_FMO_BIT | HCR_IMO_BIT | HCR_AMO_BIT |
				HCR_FB_BIT | HCR_BSU_IS_VAL | HCR_TWI_BIT | HCR_TWE_BIT |
				HCR_TSC_BIT | HCR_RW_BIT | HCR_FWB_BIT;

	/* Set VTCR_EL2, need to reconfigure according to stage2 configuration
	 * VTCR_SL0: Starting level of the stage 2 translation lookup
	 * VTCR_T0SZ: The size offset of the memory region addressed by VTTBR_EL2
	 * VTCR_PS: Physical address size of the second stage translation
	 */
	prec->sysregs.vtcr_el2 = VTCR_INIT_FLAGS_EL2 |
				 VTCR_SL0_EL2(prd->rtt_level_start) | VTCR_T0SZ_EL2(prd->ipa_width) |
				 VTCR_PS_32BIT_EL2;

	prec->sysregs.vttbr_el2 = prd->rtt_base | ((uint64_t)prd->vmid << 48);
	prec->sysregs.vmpidr_el2 = mpidr2reg(mpidr);
	ret = RMI_SUCCESS;

out:
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static trp_args_t *trp_rec_destroy (uint64_t rec)
{
	uint64_t ret;
	spin_lock(&rmm_lock);
	rmm_verbose("destroy REC 0x%" PRIx64 "\n", rec);

	if (!rmm_assert_granule_state(rec, GS_REC)) {
		rmm_info("destroy rec 0x%" PRIx64 " with state %d\n", rec,
			 rmm_get_granule_state(rec));
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	rmm_rec_t *prec = (rmm_rec_t *)rec;
	if (prec->state_running) {
		rmm_info("destroy a running rec\n");
		ret = RMI_ERROR_IN_USE;
		goto out;
	}

	assert(prec->owner_rd->rec_count);
	prec->owner_rd->rec_count --;

	rmm_set_granule_state(rec, GS_DELEGATED);
	ret = RMI_SUCCESS;

out:
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}


static uint64_t enter_realm(rmm_rec_t *prec)
{
	uint64_t exit_code;
	uint64_t saved_sre_el1 = read_icc_sre_el1();

	/* We do not need to save any RMM system registers */
	write_icc_sre_el1(ICC_SRE_EL1_SRE | ICC_SRE_EL1_DFB | ICC_SRE_EL1_DIB);

	/* Restore Realm REC system registers */
	write_elr_el2(prec->pc);
	write_hcr_el2(prec->sysregs.hcr_el2);
	write_spsr_el2(prec->sysregs.spsr_el2);
	write_vtcr_el2(prec->sysregs.vtcr_el2);
	write_vttbr_el2(prec->sysregs.vttbr_el2);
	write_vmpidr_el2(prec->sysregs.vmpidr_el2);
	el1_sysregs_context_restore(&(prec->sysregs.el1regs));

	/* Restore Realm REC vgic cpu_if registers */
	rmm_vgic_v3_restore_state(&prec->sysregs.cpu_if);

	/* Save & Restore GP registers; ERET; exception jump back */
	exit_code = __realm_enter(prec);

	/* Save Realm system registers to rmm_rec_t.
	 * No need to save all Realm EL2 registers because they aren't changed. */
	el1_sysregs_context_save(&(prec->sysregs.el1regs));
	prec->pc = read_elr_el2();
	prec->sysregs.spsr_el2 = read_spsr_el2();
	prec->sysregs.esr_el2 = read_esr_el2();
	prec->sysregs.far_el2 = read_far_el2();
	prec->sysregs.hpfar_el2 = read_hpfar_el2();

	/* Save Realm REC vgic cpu_if registers */
	rmm_vgic_v3_save_state(&prec->sysregs.cpu_if);

	write_icc_sre_el1(saved_sre_el1);

	return exit_code;
}

#define ESR_ISS_SRT(esr) (((esr) >> 16) & 0x1fUL)
#define ESR_ISS_ISV_MASK (1UL << 24)
#define ESR_ISS_WNR_MASK (1UL << 6)
#define HPFAR_EL2_FIPA 0xFFFFFFFFFF0UL

static void handle_data_abort_entry (rmm_rec_t *prec, rmi_rec_entry_t *pentry)
{
	if (!pentry->is_emulated_mmio)
		return;
#ifndef RME_DEBUG
	assert(prec->emulatable_abort);
#endif
	prec->pc += 4;
	if (!(prec->sysregs.esr_el2 & ESR_ISS_WNR_MASK))
		prec->gprs[ESR_ISS_SRT(prec->sysregs.esr_el2)] = pentry->emulated_read_value;
}

static void handle_data_abort_exit (rmm_rec_t *prec, rmi_rec_exit_t *pexit)
{
	const rmm_realm_t *prd = prec->owner_rd;
	pexit->reason = RMI_EXIT_SYNC;
	pexit->hpfar = prec->sysregs.hpfar_el2;
	uint64_t hpfar_el2_fipa = (prec->sysregs.hpfar_el2 & HPFAR_EL2_FIPA) << 8;
	bool fault_in_protected = prd->par_base <= hpfar_el2_fipa &&
		hpfar_el2_fipa < prd->par_base + prd->par_size;
#ifdef RME_DEBUG
	if (prec->sysregs.esr_el2 & ESR_ISS_ISV_MASK) {
		if (fault_in_protected) {
			static unsigned int warn_count = 0;
			if (warn_count++ < 3) {
				rmm_warn("treating protected data abort as emulatable."
						" esr=0x%lx, hpfar=0x%lx, far=0x%lx, pc=0x%lx\n",
						prec->sysregs.esr_el2, prec->sysregs.hpfar_el2,
						prec->sysregs.far_el2, prec->pc);
			}
		}
#else
	if ((prec->sysregs.esr_el2 & ESR_ISS_ISV_MASK) && !fault_in_protected) {
#endif
		prec->emulatable_abort = true;
		pexit->esr = prec->sysregs.esr_el2; // TODO mask away confidential bits
		pexit->far_ = prec->sysregs.far_el2; // TODO mask away page offset
		if (prec->sysregs.esr_el2 & ESR_ISS_WNR_MASK)
			pexit->emulated_write_value = prec->gprs[ESR_ISS_SRT(prec->sysregs.esr_el2)];
	} else { // non-emulatable
		prec->emulatable_abort = false;
		pexit->esr = prec->sysregs.esr_el2; // TODO mask away confidential bits
	}
}

static inline uint64_t handle_exception_trap_enter(rmm_rec_t *prec, rmi_rec_entry_t *pentry)
{
	int i;

	switch (EC_BITS(prec->sysregs.esr_el2)) {
	case EC_WFE_WFI:
		prec->pc += 4;
		rmm_verbose("handle_rec_entry: wfe_wfi(0x%" PRIx64 ")\n", prec->pc);
		break;
	case EC_AARCH64_HVC:
		for (i = 0; i < 7; i ++)
			prec->gprs[i] = pentry->gprs[i];
		break;
	case EC_AARCH64_SMC:
		if (prec->gprs[0] == PSCI_AFFINITY_INFO_AARCH64 ||
				prec->gprs[0] == PSCI_CPU_ON_AARCH64) {
			prec->gprs[0] = prec->psci_complete_result;
		} else {
			prec->gprs[0] = pentry->gprs[0];
		}
		prec->pc += 4;
		break;
	case EC_IABORT_LOWER_EL:
		break;
	case EC_DABORT_LOWER_EL:
		handle_data_abort_entry(prec, pentry);
		break;
	default:
		rmm_err("Unimplemented entry EC: esr=0x%lx\n", prec->sysregs.esr_el2);
		return RMI_ERROR_REC;
	}

	return RMI_SUCCESS;
}

static uint64_t handle_rec_entry(rmm_rec_t *prec, rmi_rec_entry_t *pentry)
{
	int i;
	uint64_t ret;

	switch (prec->enter_reason) {
	case REC_ENTER_REASON_FIRST_RUN:
	case REC_ENTER_REASON_FIQ:
	case REC_ENTER_REASON_IRQ:
		break;
	case REC_ENTER_REASON_TRAP:
		ret = handle_exception_trap_enter(prec, pentry);
		if (ret)
			return ret;
		break;
	default:
		rmm_err("Unknown enter reason: 0x%" PRIx64 "\n", prec->enter_reason);
		return RMI_ERROR_REC;
	}

	prec->sysregs.cpu_if.vgic_hcr =
		(pentry->gicv3_hcr & REC_ENTRY_ICH_HCR_VALID_MASK) | ICH_HCR_EN;
	for (i = 0; i < 16; i++) {
		prec->sysregs.cpu_if.vgic_lr[i] = pentry->gicv3_lrs[i];
	}

	return RMI_SUCCESS;
}

static void handle_psci_exit(rmm_rec_t *prec, rmi_rec_exit_t *pexit)
{
	pexit->reason = RMI_EXIT_PSCI;
	pexit->gprs[0] = prec->gprs[0];
	switch (prec->gprs[0]) {
	case PSCI_AFFINITY_INFO_AARCH64:
		pexit->gprs[0] = prec->gprs[0];
		pexit->gprs[1] = prec->gprs[1];
		pexit->gprs[2] = prec->gprs[2];
		prec->psci_pending = true;
		break;
	case PSCI_CPU_OFF:
		prec->runnable = false;
		pexit->gprs[0] = prec->gprs[0];
		break;
	case PSCI_CPU_ON_AARCH64:
		pexit->gprs[0] = prec->gprs[0];
		pexit->gprs[1] = prec->gprs[1];
		pexit->gprs[2] = prec->gprs[2];
		pexit->gprs[3] = prec->gprs[3];
		prec->psci_pending = true;
		break;
	case PSCI_CPU_SUSPEND_AARCH64:
		pexit->gprs[0] = prec->gprs[0];
		pexit->gprs[1] = prec->gprs[1];
		pexit->gprs[2] = prec->gprs[2];
		pexit->gprs[3] = prec->gprs[3];
		break;
	case PSCI_SYSTEM_OFF:
		pexit->gprs[0] = prec->gprs[0];
		prec->owner_rd->state = RS_SYSTEM_OFF;
		break;
	case PSCI_SYSTEM_RESET:
		pexit->gprs[0] = prec->gprs[0];
		prec->owner_rd->state = RS_SYSTEM_OFF;
		break;
	default:
		rmm_err("handle_psci with unknown fid=0x%lx, pc=0x%lx\n", prec->gprs[0], prec->pc);
	}
}

static void handle_exception_trap_exit(rmm_rec_t *prec, rmi_rec_exit_t *pexit)
{
	int i;
	uint64_t err_code = EC_BITS(prec->sysregs.esr_el2);

	switch (err_code) {
	case EC_WFE_WFI:
		pexit->reason = RMI_EXIT_SYNC;
		pexit->esr = prec->sysregs.esr_el2;
		rmm_verbose("handle_exception_trap: wfe_wfi(0x%" PRIx64 ")\n", prec->pc);
		break;
	case EC_AARCH64_HVC:
		pexit->reason = RMI_EXIT_SYNC;
		pexit->esr = prec->sysregs.esr_el2; // TODO mask only ESR_EL2.EC and ESR_EL2.ISS.imm16
		for (i = 0; i < 7; i ++)
			pexit->gprs[i] = prec->gprs[i];
		break;
	case EC_AARCH64_SMC:
		handle_psci_exit(prec, pexit);
		break;
	case EC_IABORT_LOWER_EL:
#ifdef RME_DEBUG
		{
			static unsigned int warn_count = 0;
			if (warn_count++ < 3) {
				rmm_warn("Treating ins abort as data abort."
						" esr=0x%lx, hpfar=0x%lx, far=0x%lx, pc=0x%lx\n",
						prec->sysregs.esr_el2, prec->sysregs.hpfar_el2,
						prec->sysregs.far_el2, prec->pc);
			}
		}
		// Intentional fallthrough
#else
		pexit->reason = RMI_EXIT_SYNC;
		pexit->esr = prec->sysregs.esr_el2; // TODO mask away confidential bits
		pexit->hpfar = prec->sysregs.hpfar_el2;
		break;
#endif
	case EC_DABORT_LOWER_EL:
		handle_data_abort_exit(prec, pexit);
		break;
	default:
		rmm_err("Unimplemented exit EC: esr=0x%lx, pc=0x%lx, x0=0x%lx\n", prec->sysregs.esr_el2, prec->pc, prec->gprs[0]);
		__builtin_unreachable();
	}

	return;
}

static uint64_t handle_rec_exit(rmm_rec_t *prec, rmi_rec_exit_t *pexit, uint64_t exit_code)
{
	int i;

	pexit->cntv_ctl =  read_ctx_reg(&(prec->sysregs.el1regs), CTX_CNTV_CTL_EL0);
	pexit->cntv_cval = read_ctx_reg(&(prec->sysregs.el1regs), CTX_CNTV_CVAL_EL0);
	/* disable el0 vtimer */
	write_cntv_ctl_el0(0);

	pexit->cntp_ctl =  read_ctx_reg(&(prec->sysregs.el1regs), CTX_CNTP_CTL_EL0);
	pexit->cntp_cval = read_ctx_reg(&(prec->sysregs.el1regs), CTX_CNTP_CVAL_EL0);
	/* disable el0 ptimer */
	write_cntp_ctl_el0(0);

	/* Expose non-confidential gicv3 context info of rec to NS through rec_exit */
	pexit->gicv3_hcr = prec->sysregs.cpu_if.vgic_hcr & REC_EXIT_ICH_HCR_VALID_MASK;
	pexit->gicv3_vmcr = prec->sysregs.cpu_if.vgic_vmcr;
	/* ICH_MISR_EL2 is RO, so we do not need to store it in rec cpu_if */
	pexit->gicv3_misr = read_ich_misr_el2();
	for (i = 0; i < 16; i++)
		pexit->gicv3_lrs[i] = prec->sysregs.cpu_if.vgic_lr[i];

	rmm_verbose("handle_rec_exit: exit_code(0x%" PRIx64 ") cntv(0x%"
			PRIx64 ", 0x%" PRIx64 ") cntp(0x%" PRIx64 ", 0x%" PRIx64
			") gicv3_hcr(0x%" PRIx64 ") gicv3_vmcr(0x%" PRIx64
			") gicv3_misr(0x%" PRIx64 ") gicv3_lrs(0x%" PRIx64 " 0x%" PRIx64
			" 0x%" PRIx64 " 0x%" PRIx64 ")\n",
			exit_code, pexit->cntv_ctl, pexit->cntv_cval,
			pexit->cntp_ctl, pexit->cntp_cval,
			pexit->gicv3_hcr, pexit->gicv3_vmcr, pexit->gicv3_misr,
			pexit->gicv3_lrs[0], pexit->gicv3_lrs[1], pexit->gicv3_lrs[2], pexit->gicv3_lrs[3]);

	switch (exit_code) {
	case ARM_EXCEPTION_FIQ:
		pexit->reason = RMI_EXIT_FIQ;
		pexit->esr = 0;
		prec->enter_reason = REC_ENTER_REASON_FIQ;
		break;
	case ARM_EXCEPTION_IRQ:
		pexit->reason = RMI_EXIT_IRQ;
		pexit->esr = 0;
		prec->enter_reason = REC_ENTER_REASON_IRQ;
		break;
	case ARM_EXCEPTION_TRAP:
		handle_exception_trap_exit(prec, pexit);
		prec->enter_reason = REC_ENTER_REASON_TRAP;
		break;
	default:
		rmm_err("Unhandled exception exit: %d",(int)exit_code);
		return RMI_ERROR_INTERNAL;
	}

	return RMI_SUCCESS;
}

/* Return true if need to call enter_realm immidiately.
 * Return false if need to return to host. */
static bool handle_internal_rec_exit (rmm_rec_t *prec, uint64_t exit_code)
{
	if (exit_code == ARM_EXCEPTION_TRAP) {
		if (EC_BITS(prec->sysregs.esr_el2) == EC_AARCH64_SMC) {
			uint64_t retval;
			rmm_realm_t *prd;
			switch (prec->gprs[0]) {
			case PSCI_CPU_OFF:
			case PSCI_CPU_SUSPEND_AARCH64:
			case PSCI_SYSTEM_OFF:
			case PSCI_SYSTEM_RESET:
				return false;
			case PSCI_VERSION:
				retval = trp_smc(set_smc_args(PSCI_VERSION, 0, 0, 0, 0, 0, 0, 0));
				prec->gprs[0] = retval;
				prec->pc += 4;
				return true;
			case PSCI_FEATURES:
				retval = trp_smc(set_smc_args(PSCI_FEATURES, prec->gprs[1], 0, 0, 0, 0, 0, 0));
				prec->gprs[0] = (uint64_t)retval;
				prec->pc += 4;
				return true;
			case PSCI_CPU_ON_AARCH64:
				prd = prec->owner_rd;
				if (mpidr2recindex(prec->gprs[1]) >= prd->rec_index) {
					prec->gprs[0] = PSCI_E_INVALID_PARAMS;
					prec->pc += 4;
					return true;
				}
				if (prec->gprs[2] < prd->par_base ||
						prec->gprs[2] >= prd->par_base + prd->par_size) {
					prec->gprs[0] = PSCI_E_INVALID_ADDRESS;
					prec->pc += 4;
					return true;
				}
				return false;
			case PSCI_AFFINITY_INFO_AARCH64:
				prd = prec->owner_rd;
				if (mpidr2recindex(prec->gprs[1]) >= prd->rec_index) {
					prec->gprs[0] = PSCI_E_INVALID_PARAMS;
					prec->pc += 4;
					return true;
				}
				if (prec->gprs[2] != 0) {
					prec->gprs[0] = PSCI_E_INVALID_PARAMS;
					prec->pc += 4;
					return true;
				}
				return false;
			default:
				prec->gprs[0] = PSCI_E_NOT_SUPPORTED;
				prec->pc += 4;
				return true;
			}
		}
	}
	return false;
}

static trp_args_t *trp_rec_enter (uint64_t rec, uint64_t run_ptr)
{
	uint64_t ret;
	uint64_t exit_code;
	spin_lock(&rmm_lock);
	rmm_verbose("enter REC 0x%" PRIx64 " 0x%" PRIx64 "\n", rec, run_ptr);

	if (!rmm_assert_granule_state(rec, GS_REC)) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (!rmm_assert_granule_state(run_ptr, GS_UNDELEGATED)) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	rmm_rec_t *prec = (rmm_rec_t *)rec;
	if (prec->state_running) {
		ret = RMI_ERROR_IN_USE;
		goto out;
	}
	if (!(prec->runnable)) {
		rmm_info("rec enter but not runnable.\n");
		ret = RMI_ERROR_REC;
		goto out;
	}
	if (prec->psci_pending) {
		rmm_info("rec enter but psci_pending\n");
		ret = RMI_ERROR_REC;
		goto out;
	}
	rmm_realm_state_e realm_state = prec->owner_rd->state;
	if (realm_state == RS_NEW || realm_state == RS_SYSTEM_OFF) {
		rmm_info("rec enter with rd.state=%d\n", realm_state);
		ret = RMI_ERROR_REALM_STATE;
		goto out;
	}

	rmi_rec_entry_t rec_entry;
	if (!rmm_read_ns(&rec_entry, (void *)run_ptr, sizeof(rec_entry))) {
		ret = RMI_ERROR_MEMORY;
		goto out;
	}
	if (rec_entry.is_emulated_mmio && !prec->emulatable_abort) {
#if RME_DEBUG
		static unsigned int warn_count = 0;
		if (warn_count++ < 3) {
			rmm_warn("rec enter: is_emulated_mmio but not emulatable."
					" esr=0x%lx, hpfar=0x%lx, far=0x%lx, pc=0x%lx\n",
					prec->sysregs.esr_el2, prec->sysregs.hpfar_el2,
					prec->sysregs.far_el2, prec->pc);
		}
#else
		rmm_info("rec enter: is_emulated_mmio but not emulatable\n");
		ret = RMI_ERROR_REC;
		goto out;
#endif
	}
	/*
	if (rec_entry.gicv3_hcr & (~REC_ENTRY_ICH_HCR_VALID_MASK)) {
		rmm_info("rec enter: invalid gicv3_hcr 0x%lx\n", rec_entry.gicv3_hcr);
		ret = RMI_ERROR_REC;
		goto out;
	}
	*/

	ret = handle_rec_entry(prec, &rec_entry);
	if (ret != RMI_SUCCESS)
		goto out;

	/* Disable the EL2 physical Timer used by host(kvm) */
	uint64_t cnthp_ctl_el2 = read_cnthp_ctl_el2();
	rmm_verbose("enter_realm enter(pc: 0x%" PRIx64 ") cnthp_ctl_el2:(0x%" PRIx64 ")\n",
		    prec->pc, cnthp_ctl_el2);
	write_cnthp_ctl_el2(cnthp_ctl_el2 & ~CNTHP_CTL_ENABLE_BIT);
	isb();

	prec->state_running = true;

	spin_unlock(&rmm_lock);
	do {
		exit_code = enter_realm(prec);
	} while (handle_internal_rec_exit(prec, exit_code));
	spin_lock(&rmm_lock);

	prec->state_running = false;
	/* Enable the EL2 physical Timer */
	/*
	if (cnthp_ctl_el2 & CNTHP_CTL_IT_STAT) {
		cnthp_ctl_el2 &= ~CNTHP_CTL_IT_MASK;
		write_cnthp_ctl_el2(cnthp_ctl_el2);
	}*/

	write_cnthp_ctl_el2(cnthp_ctl_el2);
	isb();

	rmi_rec_exit_t rec_exit;
	memset(&rec_exit, 0, sizeof(rec_exit));
	ret = handle_rec_exit(prec, &rec_exit, exit_code);
	if (ret != RMI_SUCCESS)
		goto out;

	if (!rmm_write_ns((void *)(run_ptr + sizeof(rec_entry)), &rec_exit, sizeof(rec_exit))) {
		ret = RMI_ERROR_MEMORY; // TODO: realm executed but return error seems wrong.
		goto out;
	}
	ret = RMI_SUCCESS;

out:
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static trp_args_t *trp_create_rtt (unsigned long long rtt, unsigned long long rd, unsigned long long map_addr, int level)
{
	unsigned long long ret;

	spin_lock(&rmm_lock);
	rmm_verbose("create rtt 0x%llx 0x%llx 0x%llx 0x%d\n", rtt, rd, map_addr, level);
	if (!rmm_assert_granule_state(rd, GS_RD)) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (!rmm_assert_granule_state(rtt, GS_DELEGATED)) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (!rmm_is_addr_rtt_level_aligned(map_addr, level - 1)) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	const rmm_realm_t *prd = (const rmm_realm_t *)rd;
	if (map_addr >> prd->ipa_width) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (level <= prd->rtt_level_start || level > RMM_RTT_PAGE_LEVEL) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	rmm_rtt_walk_result_t walk_result;
	rmm_rtt_walk(&walk_result, prd, map_addr, level - 1);

	if (walk_result.level != level - 1) {
		ret = RMI_ERROR_RTT_WALK;
		goto out;
	}
	rmm_rtt_entry_state_e state = rmm_get_rtte_state(&walk_result);
	if (state == ES_ASSIGNED || state == ES_VALID || state == ES_VALID_NS) {
		if (level != RMM_RTT_PAGE_LEVEL) {
			ret = RMI_ERROR_RTT_ENTRY;
			goto out;
		}
		uint64_t parent_rtte = *(walk_result.rtte);
		for (int i = 511; i >= 0; i --)
			((uint64_t *)rtt)[i] = parent_rtte + (i << 12UL);
	} else if (state == ES_UNASSIGNED) {
		memset((void *)rtt, 0, RMM_GRANULE_SIZE);
	} else if (state == ES_DESTROYED) {
		for (int i = 511; i >= 0; i --)
			((uint64_t *)rtt)[i] = 1UL << RTT_DESTROYED_BIT;
	} else if (state == ES_TABLE) {
		ret = RMI_ERROR_RTT_ENTRY;
		goto out;
	} else {
		assert(false);
	}

	rmm_set_granule_state(rtt, GS_RTT);
	rmm_set_rtte_table(&walk_result, rtt);
	ret = 0;

out:
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static trp_args_t *trp_destroy_rtt (unsigned long long rtt, unsigned long long rd, unsigned long long map_addr, int level)
{
	unsigned long long ret;

	spin_lock(&rmm_lock);
	rmm_verbose("destroy rtt 0x%llx 0x%llx 0x%llx 0x%d\n", rtt, rd, map_addr, level);
	if (!rmm_assert_granule_state(rd, GS_RD)) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (!rmm_assert_granule_state(rtt, GS_RTT)) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (!rmm_is_addr_rtt_level_aligned(map_addr, level - 1)) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	const rmm_realm_t *rdp = (const rmm_realm_t *)rd;
	if (map_addr >> rdp->ipa_width) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (level <= rdp->rtt_level_start || level > RMM_RTT_PAGE_LEVEL) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	rmm_rtt_walk_result_t walk_result;
	rmm_rtt_walk(&walk_result, rdp, map_addr, level - 1);

	if (walk_result.level != level - 1) {
		ret = RMI_ERROR_RTT_WALK;
		goto out;
	}
	if (rmm_get_rtte_state(&walk_result) != ES_TABLE) {
		ret = RMI_ERROR_RTT_ENTRY;
		goto out;
	}
	if (rmm_get_rtte_outaddr(&walk_result) != rtt) {
		ret = RMI_ERROR_RTT_ENTRY;
		goto out;
	}

	if (!rmm_rtt_fold(&walk_result)) {
		ret = RMI_ERROR_IN_USE;
		goto out;
	}
	rmm_set_granule_state(rtt, GS_DELEGATED);
	ret = 0;

out:
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static int trp_data_create_internal (uint64_t data, uint64_t rd, uint64_t map_addr, bool has_src, uint64_t src, int level)
{
	if (level != RMM_RTT_BLOCK_LEVEL && level != RMM_RTT_PAGE_LEVEL)
		return RMI_ERROR_INPUT;
	if (!rmm_assert_granule_state_level(data, level, GS_DELEGATED))
		return RMI_ERROR_INPUT;
	if (!rmm_assert_granule_state(rd, GS_RD))
		return RMI_ERROR_INPUT;
	if (has_src && !rmm_assert_granule_state_level(src, level, GS_UNDELEGATED))
		return RMI_ERROR_INPUT;
	if (!rmm_is_addr_rtt_level_aligned(map_addr, level))
		return RMI_ERROR_INPUT;
	const rmm_realm_t *rdp = (const rmm_realm_t *)rd;

	if (has_src && rdp->state != RS_NEW) {
#if RME_DEBUG
		static unsigned int warn_count = 3;
		if (warn_count++ < 3) {
			rmm_warn("late data_create. state=%d, data=0x%lx, map_addr=0x%lx, src=0x%lx\n",
					rdp->state, data, map_addr, src);
		}
#else
		return RMI_ERROR_REALM_STATE;
#endif
	}

	const uint64_t data_size = RMM_GRANULE_SIZE << ((3 - level) * 9);
	if (map_addr < rdp->par_base ||
			map_addr + data_size > rdp->par_base + rdp->par_size)
		return RMI_ERROR_INPUT;

	rmm_rtt_walk_result_t walk_result;
	rmm_rtt_walk(&walk_result, rdp, map_addr, level);

	if (walk_result.level != level)
		return RMI_ERROR_RTT_WALK;
	if (rmm_get_rtte_state(&walk_result) != ES_UNASSIGNED)
		return RMI_ERROR_RTT_ENTRY;

	if (has_src) {
		/* rmm_read_ns doesn't support multiple page, so we have to break up here. */
		for (uint64_t offset = 0; offset < data_size; offset += RMM_GRANULE_SIZE) {
			if (!rmm_read_ns((void *)(data + offset), (void *)(src + offset), RMM_GRANULE_SIZE))
				return RMI_ERROR_MEMORY;
		}
	} else {
		memset((void *)data, 0, data_size);
	}
	clean_dcache_range(data, data_size);
	for (uint64_t offset = 0; offset < data_size; offset += RMM_GRANULE_SIZE) {
		rmm_set_granule_state(data + offset, GS_DATA);
	}
	rmm_set_rtte(&walk_result, data, false);
	return 0;
}

static trp_args_t *trp_data_create (unsigned long long data, unsigned long long rd, unsigned long long map_addr, unsigned long long src, unsigned long long level)
{
	spin_lock(&rmm_lock);
	rmm_verbose("create data 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx\n", data, rd, map_addr, src, level);
	int ret = trp_data_create_internal(data, rd, map_addr, true, src, level);
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static trp_args_t *trp_data_create_unknown (unsigned long long data, unsigned long long rd, unsigned long long map_addr, unsigned long long level)
{
	spin_lock(&rmm_lock);
	rmm_verbose("create data unknown 0x%llx 0x%llx 0x%llx 0x%llx\n", data, rd, map_addr, level);
	int ret = trp_data_create_internal(data, rd, map_addr, false, 0, level);
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static int trp_data_destroy_internal (uint64_t rd, uint64_t map_addr, int level)
{
	if (level != RMM_RTT_BLOCK_LEVEL && level != RMM_RTT_PAGE_LEVEL)
		return RMI_ERROR_INPUT;
	if (!rmm_assert_granule_state(rd, GS_RD))
		return RMI_ERROR_INPUT;
	if (!rmm_is_addr_granule_aligned(map_addr))
		return RMI_ERROR_INPUT;
	const rmm_realm_t *rdp = (const rmm_realm_t *)rd;
	const uint64_t data_size = RMM_GRANULE_SIZE << ((3 - level) * 9);
	if (map_addr < rdp->par_base || map_addr + data_size > rdp->par_base + rdp->par_size)
		return RMI_ERROR_INPUT;

	rmm_rtt_walk_result_t walk_result;
	rmm_rtt_walk(&walk_result, rdp, map_addr, level);

	if (walk_result.level != level)
		return RMI_ERROR_RTT_WALK;
	if (rmm_get_rtte_state(&walk_result) != ES_ASSIGNED)
		return RMI_ERROR_RTT_ENTRY;
	if (!rmm_assert_granule_state_level(rmm_get_rtte_outaddr(&walk_result), level, GS_DATA)) {
		rmm_err("Destroying data at %lx but found type %d\n",
				(unsigned long)rmm_get_rtte_outaddr(&walk_result),
				rmm_get_granule_state(rmm_get_rtte_outaddr(&walk_result)));
	}

	for (uint64_t offset = 0; offset < data_size; offset += RMM_GRANULE_SIZE) {
		rmm_set_granule_state(rmm_get_rtte_outaddr(&walk_result) + offset, GS_DELEGATED);
	}
	rmm_set_rtte_destroyed(&walk_result);
	return 0;
}

static trp_args_t *trp_data_destroy (unsigned long long rd, unsigned long long map_addr, unsigned long long level)
{
	spin_lock(&rmm_lock);
	rmm_verbose("destroy data 0x%llx 0x%llx 0x%llx\n", rd, map_addr, level);
	int ret = trp_data_destroy_internal(rd, map_addr, level);
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static int trp_data_dispose_internal (uint64_t rd, uint64_t rec, uint64_t map_addr, int level)
{
	if (level != RMM_RTT_BLOCK_LEVEL && level != RMM_RTT_PAGE_LEVEL)
		return RMI_ERROR_INPUT;
	if (!rmm_is_addr_rtt_level_aligned(map_addr, level))
		return RMI_ERROR_INPUT;
	if (!rmm_assert_granule_state(rd, GS_RD))
		return RMI_ERROR_INPUT;
	if (!rmm_assert_granule_state(rec, GS_REC))
		return RMI_ERROR_INPUT;
	const rmm_realm_t *prd = (const rmm_realm_t *)rd;
	const rmm_rec_t *prec = (const rmm_rec_t *)rec;
	if (prec->state_running)
		return RMI_ERROR_IN_USE;
	if (prec->owner_rd != prd)
		return RMI_ERROR_OWNER;
	//TODO: prec->dispose_base and dispose_size

	rmm_rtt_walk_result_t walk_result;
	rmm_rtt_walk(&walk_result, prd, map_addr, level);

	if (walk_result.level != level)
		return RMI_ERROR_RTT_WALK;
	if (rmm_get_rtte_state(&walk_result) != ES_DESTROYED)
		return RMI_ERROR_RTT_ENTRY;

	rmm_set_rtte(&walk_result, 0, false);
	return 0;
}

static trp_args_t *trp_data_dispose (unsigned long long rd, unsigned long long rec, unsigned long long map_addr, int level)
{
	spin_lock(&rmm_lock);
	rmm_verbose("dispose data 0x%llx 0x%llx0x%llx %d\n", rd, rec, map_addr, level);
	int ret = trp_data_dispose_internal(rd, rec, map_addr, level);
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static int trp_map_unmap_internal(unsigned long long rd, unsigned long long map_addr, int level, int is_map, unsigned long long ns_rtte)
{
	if (level != RMM_RTT_BLOCK_LEVEL && level != RMM_RTT_PAGE_LEVEL)
		return RMI_ERROR_INPUT;
	if (!rmm_assert_granule_state(rd, GS_RD))
		return RMI_ERROR_INPUT;
	if (!rmm_is_addr_rtt_level_aligned(map_addr, level))
		return RMI_ERROR_INPUT;
	if (is_map && ns_rtte) {
		uint64_t oa = ns_rtte & RTT_OA_MASK; // should we check if it's ns?
		if (!rmm_is_addr_delegable(oa)) // TODO: is this same as PaIsValid?
			return RMI_ERROR_INPUT;
		if (!rmm_is_addr_rtt_level_aligned(oa, level))
			return RMI_ERROR_INPUT;
		//TODO check RttEntryIsValidForUnprotected
	}
	const rmm_realm_t *prd = (const rmm_realm_t *)rd;
	if (map_addr >> prd->ipa_width)
		return RMI_ERROR_INPUT;

	rmm_rtt_walk_result_t walk_result;
	rmm_rtt_walk(&walk_result, prd, map_addr, level);

	if (walk_result.level != level)
		return RMI_ERROR_RTT_WALK;
	rmm_rtt_entry_state_e state = rmm_get_rtte_state(&walk_result);
	if (is_map) {
		if (ns_rtte) {
			if (state != ES_UNASSIGNED && state != ES_DESTROYED)
				return RMI_ERROR_RTT_ENTRY;
			rmm_set_rtte_ns(&walk_result, ns_rtte);
		} else {
			if (state != ES_ASSIGNED)
				return RMI_ERROR_RTT_ENTRY;
			rmm_set_rtte_valid(&walk_result, true);
		}
	} else {
		if (ns_rtte) {
			if (state != ES_VALID_NS)
				return RMI_ERROR_RTT_ENTRY;
			if (rmm_get_rtte_outaddr(&walk_result) != ns_rtte)
				return RMI_ERROR_RTT_ENTRY;
			rmm_set_rtte(&walk_result, 0, false);
		} else {
			if (state != ES_VALID)
				return RMI_ERROR_RTT_ENTRY;
			rmm_set_rtte_valid(&walk_result, false);
		}
	}

	/* TLB maintenance
	 * TODO: check VMID switch and barrier etc*/
	asm volatile("tlbi ipas2le1is, %0" : : "r" (TLBI_ADDR(map_addr)));
	return 0;
}

static trp_args_t *trp_map_protected (unsigned long long rd, unsigned long long map_addr, int level)
{
	spin_lock(&rmm_lock);
	unsigned long long ret = trp_map_unmap_internal(rd, map_addr, level, true, 0);
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static trp_args_t *trp_map_unprotected (unsigned long long rd, unsigned long long map_addr, int level, unsigned long long rtte)
{
	spin_lock(&rmm_lock);
	rmm_verbose("map unprotected 0x%llx 0x%llx 0x%d 0x%llx\n", rd, map_addr, level, rtte);
	unsigned long long ret = trp_map_unmap_internal(rd, map_addr, level, true, rtte);
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static trp_args_t *trp_unmap_protected (unsigned long long rd, unsigned long long map_addr, int level)
{
	spin_lock(&rmm_lock);
	rmm_verbose("unmap protected 0x%llx 0x%llx 0x%d\n", rd, map_addr, level);
	unsigned long long ret = trp_map_unmap_internal(rd, map_addr, level, false, 0);
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static trp_args_t *trp_unmap_unprotected (unsigned long long rd, unsigned long long map_addr, int level, unsigned long long rtte)
{
	spin_lock(&rmm_lock);
	rmm_verbose("unmap unprotected 0x%llx 0x%llx 0x%d 0x%llx\n", rd, map_addr, level, rtte);
	unsigned long long ret = trp_map_unmap_internal(rd, map_addr, level, false, rtte);
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static trp_args_t *trp_read_rtt_entry (unsigned long long rd, unsigned long long map_addr, int level)
{
	unsigned long long ret;
	spin_lock(&rmm_lock);
	rmm_verbose("read_rtt_entry 0x%llx 0x%llx 0x%d\n", rd, map_addr, level);
	if (!rmm_assert_granule_state(rd, GS_RD)) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (!rmm_is_addr_rtt_level_aligned(map_addr, level)) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	const rmm_realm_t *prd = (const rmm_realm_t *)rd;
	if (map_addr >> prd->ipa_width) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (level < prd->rtt_level_start || level > RMM_RTT_PAGE_LEVEL) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	rmm_rtt_walk_result_t walk_result;
	rmm_rtt_walk(&walk_result, prd, map_addr, level);

	if (walk_result.level != level) {
		ret = RMI_ERROR_RTT_WALK;
		goto out;
	}

	uint64_t rtte = *(walk_result.rtte);
	rmm_verbose("read entry: addr=%lx, level=%d, rtte=%lx\n", (unsigned long)map_addr, level, (unsigned long)rtte);
	rmm_rtt_entry_state_e state = rmm_get_rtte_state(&walk_result);
	switch (state) {
	case ES_UNASSIGNED:
	case ES_DESTROYED:
		rtte = 0;
		break;
	case ES_ASSIGNED:
	case ES_VALID:
	case ES_TABLE:
		rtte &= RTT_OA_MASK;
		break;
	case ES_VALID_NS:
		rtte &= RTT_OA_MASK | 0x3fcUL; // 0x3fc = MemAttr[2:5], S2AP[6:7], SH[8:9]
		break;
	default:
		assert(false);
	}
	ret = RMI_SUCCESS;

out:
	spin_unlock(&rmm_lock);
	if (ret == RMI_SUCCESS)
		return set_smc_args(RMMD_RMI_REQ_COMPLETE, 0, level, state, rtte, 0, 0, 0);
	else
		return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}

static trp_args_t *trp_psci_complete (uint64_t calling_rec, uint64_t target_rec)
{
	unsigned long long ret;
	spin_lock(&rmm_lock);
	rmm_verbose("trp_psci_complete 0x%lx 0x%lx\n", calling_rec, target_rec);
	if (calling_rec == target_rec) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (!rmm_assert_granule_state(calling_rec, GS_REC)) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (!rmm_assert_granule_state(target_rec, GS_REC)) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	rmm_rec_t *prec_calling = (rmm_rec_t *)calling_rec;
	rmm_rec_t *prec_target = (rmm_rec_t *)target_rec;
	if (!prec_calling->psci_pending) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (prec_calling->owner_rd != prec_target->owner_rd) {
		ret = RMI_ERROR_INPUT;
		goto out;
	}
	if (prec_calling->gprs[0] != PSCI_AFFINITY_INFO_AARCH64 && prec_calling->gprs[0] != PSCI_CPU_ON_AARCH64) {
		rmm_err("trp_psci_complete unexpected x0=0x%lx\n", prec_calling->gprs[0]);
		ret = RMI_ERROR_INPUT;
		goto out;
	}

	prec_calling->psci_pending = false;
	if (prec_calling->gprs[0] == PSCI_AFFINITY_INFO_AARCH64) {
		prec_calling->psci_complete_result = prec_target->runnable ? AFF_STATE_ON : AFF_STATE_OFF;
	} else {
		assert(prec_calling->gprs[0] == PSCI_CPU_ON_AARCH64);
		if (prec_target->runnable) {
			prec_calling->psci_complete_result = PSCI_E_ALREADY_ON;
		} else {
			prec_calling->psci_complete_result = PSCI_E_SUCCESS;
			prec_target->gprs[0] = prec_calling->gprs[3];
			for (int i = 1; i <= 31; i ++)
				prec_target->gprs[i] = 0;
			prec_target->runnable = true;
			prec_target->pc = prec_calling->gprs[2];
		}
	}
	prec_calling->gprs[1] = 0;
	prec_calling->gprs[2] = 0;
	prec_calling->gprs[3] = 0;
	ret = RMI_SUCCESS;

out:
	spin_unlock(&rmm_lock);
	return set_smc_args(RMMD_RMI_REQ_COMPLETE, ret, 0, 0, 0, 0, 0, 0);
}


/*******************************************************************************
 * Main RMI SMC handler function
 ******************************************************************************/
trp_args_t *trp_rmi_handler(unsigned long fid, unsigned long long x1,
			    unsigned long long x2, unsigned long long x3,
				unsigned long long x4, unsigned long long x5)
{
	switch (fid) {
	case RMI_RMM_REQ_VERSION:
		return trp_ret_rmi_version();
	case RMI_RMM_GRANULE_DELEGATE:
		return trp_asc_mark_realm(x1);
	case RMI_RMM_GRANULE_UNDELEGATE:
		return trp_asc_mark_nonsecure(x1);
	case RMI_RMM_DATA_CREATE:
		return trp_data_create(x1, x2, x3, x4, 3);
	case RMI_RMM_DATA_CREATE_UNKNOWN:
		return trp_data_create_unknown(x1, x2, x3, 3);
	case RMI_RMM_DATA_CREATE_LEVEL:
		return trp_data_create(x1, x2, x3, x4, x5);
	case RMI_RMM_DATA_CREATE_UNKNOWN_LEVEL:
		return trp_data_create_unknown(x1, x2, x3, x4);
	case RMI_RMM_DATA_DESTROY:
		return trp_data_destroy(x1, x2, 3);
	case RMI_RMM_DATA_DESTROY_LEVEL:
		return trp_data_destroy(x1, x2, x3);
	case RMI_RMM_DATA_DISPOSE:
		return trp_data_dispose(x1, x2, x3, x4);
	case RMI_RMM_REALM_ACTIVATE:
		return trp_activate_realm(x1);
	case RMI_RMM_REALM_CREATE:
		return trp_create_realm(x1, x2);
	case RMI_RMM_REALM_DESTROY:
		return trp_destroy_realm(x1);
	case RMI_RMM_REC_CREATE:
		return trp_rec_create(x1, x2, x3, x4);
	case RMI_RMM_REC_DESTROY:
		return trp_rec_destroy(x1);
	case RMI_RMM_REC_ENTER:
		return trp_rec_enter(x1, x2);
	case RMI_RMM_RTT_CREATE:
		return trp_create_rtt(x1, x2, x3, x4);
	case RMI_RMM_RTT_DESTROY:
		return trp_destroy_rtt(x1, x2, x3, x4);
	case RMI_RMM_RTT_MAP_UNPROTECTED:
		return trp_map_unprotected(x1, x2, x3, x4);
	case RMI_RMM_RTT_MAP_PROTECTED:
		return trp_map_protected(x1, x2, x3);
	case RMI_RMM_RTT_READ_ENTRY:
		return trp_read_rtt_entry(x1, x2, x3);
	case RMI_RMM_RTT_UNMAP_UNPROTECTED:
		return trp_unmap_unprotected(x1, x2, x3, x4);
	case RMI_RMM_RTT_UNMAP_PROTECTED:
		return trp_unmap_protected(x1, x2, x3);
	case RMI_RMM_PSCI_COMPLETE:
		return trp_psci_complete(x1, x2);
	case RMI_RMM_FEATURES:
		return set_smc_args(RMMD_RMI_REQ_COMPLETE, RMI_ERROR_NOT_SUPPORTED, 0, 0, 0, 0, 0, 0);
	default:
		rmm_err("Invalid SMC code to %s, FID %lu\n", __func__, fid);
	}
	return set_smc_args(SMC_UNK, 0, 0, 0, 0, 0, 0, 0);
}
