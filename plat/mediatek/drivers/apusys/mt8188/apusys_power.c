/*
 * Copyright (c) 2023, MediaTek Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <inttypes.h>

/* TF-A system header */
#include <common/debug.h>
#include <drivers/delay_timer.h>
#include <lib/mmio.h>
#include <lib/utils_def.h>
#include <lib/xlat_tables/xlat_tables_v2.h>

/* Vendor header */
#include "apusys.h"
#include "apusys_power.h"
#include <mtk_mmap_pool.h>

static int apu_poll(uintptr_t reg, uint32_t mask, uint32_t value, uint32_t timeout_us)
{
	uint32_t reg_val, count;

	count = timeout_us / APU_POLL_STEP_US;
	if (count == 0) {
		count = 1;
	}

	do {
		reg_val = mmio_read_32(reg);
		if ((reg_val & mask) == value) {
			return 0;
		}

		udelay(APU_POLL_STEP_US);
	} while (--count);

	ERROR(MODULE_TAG "Timeout polling APU register %#" PRIxPTR "\n", reg);
	ERROR(MODULE_TAG "Read value 0x%x, expected 0x%x\n", reg_val,
	      (value == 0U) ? (reg_val & ~mask) : (reg_val | mask));

	return -1;
}

static void get_pll_pcw(const uint32_t clk_rate, uint32_t *r1, uint32_t *r2)
{
	unsigned int fvco = clk_rate;
	unsigned int pcw_val;
	unsigned int postdiv_val = 1;
	unsigned int postdiv_reg = 0;

	while (fvco <= OUT_CLK_FREQ_MIN) {
		postdiv_val = postdiv_val << 1;
		postdiv_reg = postdiv_reg + 1;
		fvco = fvco << 1;
	}

	pcw_val = (fvco * (1 << DDS_SHIFT)) / BASIC_CLK_FREQ;

	if (postdiv_reg == 0) {
		pcw_val = pcw_val * 2;
		postdiv_val = postdiv_val << 1;
		postdiv_reg = postdiv_reg + 1;
	}

	*r1 = postdiv_reg;
	*r2 = pcw_val;
}

static void apu_pll_init(void)
{
	const uint32_t pll_hfctl_cfg[PLL_NUM] = {
		PLL4HPLL_FHCTL0_CFG,
		PLL4HPLL_FHCTL1_CFG,
		PLL4HPLL_FHCTL2_CFG,
		PLL4HPLL_FHCTL3_CFG
	};
	const uint32_t pll_con1[PLL_NUM] = {
		PLL4H_PLL1_CON1,
		PLL4H_PLL2_CON1,
		PLL4H_PLL3_CON1,
		PLL4H_PLL4_CON1
	};
	const uint32_t pll_fhctl_dds[PLL_NUM] = {
		PLL4HPLL_FHCTL0_DDS,
		PLL4HPLL_FHCTL1_DDS,
		PLL4HPLL_FHCTL2_DDS,
		PLL4HPLL_FHCTL3_DDS
	};
	const uint32_t pll_freq_out[PLL_NUM] = {
		APUPLL0_DEFAULT_FREQ,
		APUPLL1_DEFAULT_FREQ,
		APUPLL2_DEFAULT_FREQ,
		APUPLL3_DEFAULT_FREQ
	};
	uint32_t pcw_val, posdiv_val;
	int pll_idx;

	mmio_setbits_32(APU_PLL_BASE + PLL4HPLL_FHCTL_RST_CON, PLL4H_PLL_HP_SWRSTB);
	mmio_setbits_32(APU_PLL_BASE + PLL4HPLL_FHCTL_HP_EN, PLL4H_PLL_HP_EN);
	mmio_setbits_32(APU_PLL_BASE + PLL4HPLL_FHCTL_CLK_CON, PLL4H_PLL_HP_CLKEN);

	for (pll_idx = 0; pll_idx < PLL_NUM; pll_idx++) {
		mmio_setbits_32(APU_PLL_BASE + pll_hfctl_cfg[pll_idx], (FHCTL0_EN | SFSTR0_EN));

		posdiv_val = 0;
		pcw_val = 0;
		get_pll_pcw(pll_freq_out[pll_idx], &posdiv_val, &pcw_val);

		mmio_clrsetbits_32(APU_PLL_BASE + pll_con1[pll_idx],
				   (RG_PLL_POSDIV_MASK << RG_PLL_POSDIV_SFT),
				   (posdiv_val << RG_PLL_POSDIV_SFT));
		mmio_write_32(APU_PLL_BASE + pll_fhctl_dds[pll_idx],
			      (FHCTL_PLL_TGL_ORG | pcw_val));
	}
}

static void apu_acc_init(void)
{
	mmio_write_32(APU_ACC_BASE + APU_ACC_CONFG_CLR0, CGEN_SOC);
	mmio_write_32(APU_ACC_BASE + APU_ACC_CONFG_SET0, HW_CTRL_EN);

	mmio_write_32(APU_ACC_BASE + APU_ACC_CONFG_CLR1, CGEN_SOC);
	mmio_write_32(APU_ACC_BASE + APU_ACC_CONFG_SET1, HW_CTRL_EN);

	mmio_write_32(APU_ACC_BASE + APU_ACC_CONFG_CLR2, CGEN_SOC);
	mmio_write_32(APU_ACC_BASE + APU_ACC_CONFG_SET2, HW_CTRL_EN);
	mmio_write_32(APU_ACC_BASE + APU_ACC_AUTO_CTRL_SET2, CLK_REQ_SW_EN);

	mmio_write_32(APU_ACC_BASE + APU_ACC_CONFG_CLR3, CGEN_SOC);
	mmio_write_32(APU_ACC_BASE + APU_ACC_CONFG_SET3, HW_CTRL_EN);
	mmio_write_32(APU_ACC_BASE + APU_ACC_AUTO_CTRL_SET3, CLK_REQ_SW_EN);

	mmio_write_32(APU_ACC_BASE + APU_ACC_CLK_INV_EN_SET, CLK_INV_EN);
}

static void apu_buck_off_cfg(void)
{
	mmio_write_32(APU_RPC_BASE + APU_RPC_HW_CON, BUCK_PROT_REQ_SET);
	udelay(10);

	mmio_write_32(APU_RPC_BASE + APU_RPC_HW_CON, BUCK_ELS_EN_SET);
	udelay(10);

	mmio_write_32(APU_RPC_BASE + APU_RPC_HW_CON, BUCK_AO_RST_B_CLR);
	udelay(10);
}

static void apu_pcu_init(void)
{
	uint32_t vapu_en_offset = BUCK_VAPU_PMIC_REG_EN_ADDR;
	uint32_t vapu_sram_en_offset = BUCK_VAPU_SRAM_PMIC_REG_EN_ADDR;

	mmio_write_32(APU_PCU_BASE + APU_PCU_CTRL_SET, AUTO_BUCK_EN);

	mmio_write_32(APU_PCU_BASE + APU_PCU_BUCK_STEP_SEL, BUCK_ON_OFF_CMD_EN);

	mmio_write_32(APU_PCU_BASE + APU_PCU_BUCK_ON_DAT0_L,
		      ((vapu_sram_en_offset << BUCK_OFFSET_SFT) + BUCK_ON_CMD));
	mmio_write_32(APU_PCU_BASE + APU_PCU_BUCK_ON_DAT0_H, CMD_OP);

	mmio_write_32(APU_PCU_BASE + APU_PCU_BUCK_ON_DAT1_L,
		      ((vapu_en_offset << BUCK_OFFSET_SFT) + BUCK_ON_CMD));
	mmio_write_32(APU_PCU_BASE + APU_PCU_BUCK_ON_DAT1_H, CMD_OP);

	mmio_write_32(APU_PCU_BASE + APU_PCU_BUCK_OFF_DAT0_L,
		      ((vapu_en_offset << BUCK_OFFSET_SFT) + BUCK_OFF_CMD));
	mmio_write_32(APU_PCU_BASE + APU_PCU_BUCK_OFF_DAT0_H, CMD_OP);

	mmio_write_32(APU_PCU_BASE + APU_PCU_BUCK_OFF_DAT1_L,
		      ((vapu_sram_en_offset << BUCK_OFFSET_SFT) + BUCK_OFF_CMD));
	mmio_write_32(APU_PCU_BASE + APU_PCU_BUCK_OFF_DAT1_H, CMD_OP);

	mmio_write_32(APU_PCU_BASE + APU_PCU_BUCK_ON_SLE0, APU_PCU_BUCK_ON_SETTLE_TIME);
	mmio_write_32(APU_PCU_BASE + APU_PCU_BUCK_ON_SLE1, APU_PCU_BUCK_ON_SETTLE_TIME);
}

static void apu_rpclite_init(void)
{
	const uint32_t sleep_type_offset[] = {
		APU_RPC_SW_TYPE2,
		APU_RPC_SW_TYPE3,
		APU_RPC_SW_TYPE4,
		APU_RPC_SW_TYPE5,
		APU_RPC_SW_TYPE6,
		APU_RPC_SW_TYPE7,
		APU_RPC_SW_TYPE8,
		APU_RPC_SW_TYPE9
	};
	int ofs_arr_size = ARRAY_SIZE(sleep_type_offset);
	int ofs_idx;

	for (ofs_idx = 0 ; ofs_idx < ofs_arr_size ; ofs_idx++) {
		mmio_clrbits_32(APU_ACX0_RPC_LITE_BASE + sleep_type_offset[ofs_idx],
				SW_TYPE);
	}

	mmio_setbits_32(APU_ACX0_RPC_LITE_BASE + APU_RPC_TOP_SEL, RPC_CTRL);
}

static void apu_rpc_init(void)
{
	mmio_clrbits_32(APU_RPC_BASE + APU_RPC_SW_TYPE0, SW_TYPE);
	mmio_setbits_32(APU_RPC_BASE + APU_RPC_TOP_SEL, RPC_TOP_CTRL);
	mmio_setbits_32(APU_RPC_BASE + APU_RPC_TOP_SEL_1, RPC_TOP_CTRL1);
}

static int apu_are_init(void)
{
	int ret;
	int are_id = 0;
	const uint32_t are_base[APU_ARE_NUM] = { APU_ARE0_BASE, APU_ARE1_BASE, APU_ARE2_BASE };
	const uint32_t are_entry2_cfg_l[APU_ARE_NUM] = {
		ARE0_ENTRY2_CFG_L,
		ARE1_ENTRY2_CFG_L,
		ARE2_ENTRY2_CFG_L
	};

	mmio_setbits_32(APU_AO_CTL_BASE + CSR_DUMMY_0_ADDR, VCORE_ARE_REQ);

	ret = apu_poll(APU_ARE2_BASE + APU_ARE_GLO_FSM, ARE_GLO_FSM_IDLE, ARE_GLO_FSM_IDLE,
		       APU_ARE_POLLING_TIMEOUT_US);
	if (ret != 0) {
		ERROR(MODULE_TAG "[%s][%d] ARE init timeout\n",
		      __func__, __LINE__);
		return ret;
	}

	for (are_id = APU_ARE0; are_id < APU_ARE_NUM; are_id++) {
		mmio_write_32(are_base[are_id] + APU_ARE_ENTRY0_SRAM_H, ARE_ENTRY0_SRAM_H_INIT);
		mmio_write_32(are_base[are_id] + APU_ARE_ENTRY0_SRAM_L, ARE_ENTRY0_SRAM_L_INIT);

		mmio_write_32(are_base[are_id] + APU_ARE_ENTRY1_SRAM_H, ARE_ENTRY1_SRAM_H_INIT);
		mmio_write_32(are_base[are_id] + APU_ARE_ENTRY1_SRAM_L, ARE_ENTRY1_SRAM_L_INIT);

		mmio_write_32(are_base[are_id] + APU_ARE_ENTRY2_SRAM_H, ARE_ENTRY_CFG_H);
		mmio_write_32(are_base[are_id] + APU_ARE_ENTRY2_SRAM_L, are_entry2_cfg_l[are_id]);

		mmio_read_32(are_base[are_id] + APU_ARE_ENTRY2_SRAM_H);
		mmio_read_32(are_base[are_id] + APU_ARE_ENTRY2_SRAM_L);

		mmio_write_32(are_base[are_id] + APU_ARE_INI_CTRL, ARE_CONFG_INI);
	}

	return ret;
}

static void apu_aoc_init(void)
{
	mmio_clrbits_32(SPM_BASE + APUSYS_BUCK_ISOLATION, IPU_EXT_BUCK_ISO);
	mmio_write_32(APU_RPC_BASE + APU_RPC_HW_CON, BUCK_ELS_EN_CLR);
	udelay(10);

	mmio_write_32(APU_RPC_BASE + APU_RPC_HW_CON, BUCK_AO_RST_B_SET);
	udelay(10);

	mmio_write_32(APU_RPC_BASE + APU_RPC_HW_CON, BUCK_PROT_REQ_CLR);
	udelay(10);

	mmio_write_32(APU_RPC_BASE + APU_RPC_HW_CON, SRAM_AOC_ISO_CLR);
	udelay(10);
}

static int init_hw_setting(void)
{
	int ret;

	apu_aoc_init();
	apu_pcu_init();
	apu_rpc_init();
	apu_rpclite_init();

	ret = apu_are_init();
	if (ret != 0) {
		return ret;
	}

	apu_pll_init();
	apu_acc_init();
	apu_buck_off_cfg();

	return ret;
}

int apusys_power_init(void)
{
	int ret;

	ret = init_hw_setting();
	if (ret != 0) {
		ERROR(MODULE_TAG "%s initial fail\n", __func__);
	} else {
		INFO(MODULE_TAG "%s initial done\n", __func__);
	}

	return ret;
}
