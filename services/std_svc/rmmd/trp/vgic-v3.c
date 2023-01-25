#include <arch.h>
#include "trp_private.h"
#include "vgic-v3.h"

static void rmm_gic_v3_set_lr(uint64_t val, int lr)
{
	switch (lr & 0xf) {
	case 0:
		write_ich_lr0_el2(val);
		break;
	case 1:
		write_ich_lr1_el2(val);
		break;
	case 2:
		write_ich_lr2_el2(val);
		break;
	case 3:
		write_ich_lr3_el2(val);
		break;
	case 4:
		write_ich_lr4_el2(val);
		break;
	case 5:
		write_ich_lr5_el2(val);
		break;
	case 6:
		write_ich_lr6_el2(val);
		break;
	case 7:
		write_ich_lr7_el2(val);
		break;
	case 8:
		write_ich_lr8_el2(val);
		break;
	case 9:
		write_ich_lr9_el2(val);
		break;
	case 10:
		write_ich_lr10_el2(val);
		break;
	case 11:
		write_ich_lr11_el2(val);
		break;
	case 12:
		write_ich_lr12_el2(val);
		break;
	case 13:
		write_ich_lr13_el2(val);
		break;
	case 14:
		write_ich_lr14_el2(val);
		break;
	case 15:
		write_ich_lr15_el2(val);
		break;
	}
}

static uint64_t rmm_gic_v3_get_lr(unsigned int lr)
{
	switch (lr & 0xf) {
	case 0:
		return read_ich_lr0_el2();
	case 1:
		return read_ich_lr1_el2();
	case 2:
		return read_ich_lr2_el2();
	case 3:
		return read_ich_lr3_el2();
	case 4:
		return read_ich_lr4_el2();
	case 5:
		return read_ich_lr5_el2();
	case 6:
		return read_ich_lr6_el2();
	case 7:
		return read_ich_lr7_el2();
	case 8:
		return read_ich_lr8_el2();
	case 9:
		return read_ich_lr9_el2();
	case 10:
		return read_ich_lr10_el2();
	case 11:
		return read_ich_lr11_el2();
	case 12:
		return read_ich_lr12_el2();
	case 13:
		return read_ich_lr13_el2();
	case 14:
		return read_ich_lr14_el2();
	case 15:
		return read_ich_lr15_el2();
	}

	return 0;
}

static uint32_t rmm_vgic_v3_read_ap0rn(int n)
{
	uint32_t val;

	switch (n) {
	case 0:
		val = read_ich_ap0r0_el2();
		break;
	case 1:
		val = read_ich_ap0r1_el2();
		break;
	case 2:
		val = read_ich_ap0r2_el2();
		break;
	case 3:
		val = read_ich_ap0r3_el2();
		break;
	default:
		__builtin_unreachable();
	}

	return val;
}

static uint32_t rmm_vgic_v3_read_ap1rn(int n)
{
	uint32_t val;

	switch (n) {
	case 0:
		val = read_ich_ap1r0_el2();
		break;
	case 1:
		val = read_ich_ap1r1_el2();
		break;
	case 2:
		val = read_ich_ap1r2_el2();
		break;
	case 3:
		val = read_ich_ap1r3_el2();
		break;
	default:
		__builtin_unreachable();
	}

	return val;
}

static void rmm_vgic_v3_write_ap0rn(uint32_t val, int n)
{
	switch (n) {
	case 0:
		write_ich_ap0r0_el2(val);
		break;
	case 1:
		write_ich_ap0r1_el2(val);
		break;
	case 2:
		write_ich_ap0r2_el2(val);
		break;
	case 3:
		write_ich_ap0r3_el2(val);
		break;
	default:
		__builtin_unreachable();
	}
}

static void rmm_vgic_v3_write_ap1rn(uint32_t val, int n)
{
	switch (n) {
	case 0:
		write_ich_ap1r0_el2(val);
		break;
	case 1:
		write_ich_ap1r1_el2(val);
		break;
	case 2:
		write_ich_ap1r2_el2(val);
		break;
	case 3:
		write_ich_ap1r3_el2(val);
		break;
	default:
		__builtin_unreachable();
	}
}

static void rmm_vgic_v3_save_aprs(vgic_v3_cpu_if_t *cpu_if)
{
	uint64_t val;
	uint32_t nr_pre_bits;

	val = read_ich_vtr_el2();
	nr_pre_bits = vtr_to_nr_pre_bits(val);

	switch (nr_pre_bits) {
	case 7:
		cpu_if->vgic_ap0r[3] = rmm_vgic_v3_read_ap0rn(3);
		cpu_if->vgic_ap0r[2] = rmm_vgic_v3_read_ap0rn(2);
		__attribute__((__fallthrough__));
	case 6:
		cpu_if->vgic_ap0r[1] = rmm_vgic_v3_read_ap0rn(1);
		__attribute__((__fallthrough__));
	default:
		cpu_if->vgic_ap0r[0] = rmm_vgic_v3_read_ap0rn(0);
	}

	switch (nr_pre_bits) {
	case 7:
		cpu_if->vgic_ap1r[3] = rmm_vgic_v3_read_ap1rn(3);
		cpu_if->vgic_ap1r[2] = rmm_vgic_v3_read_ap1rn(2);
		__attribute__((__fallthrough__));
	case 6:
		cpu_if->vgic_ap1r[1] = rmm_vgic_v3_read_ap1rn(1);
		__attribute__((__fallthrough__));
	default:
		cpu_if->vgic_ap1r[0] = rmm_vgic_v3_read_ap1rn(0);
	}
}


static void rmm_vgic_v3_restore_aprs(vgic_v3_cpu_if_t *cpu_if)
{
	uint64_t val;
	uint32_t nr_pre_bits;

	val = read_ich_vtr_el2();
	nr_pre_bits = vtr_to_nr_pre_bits(val);

	switch (nr_pre_bits) {
	case 7:
		rmm_vgic_v3_write_ap0rn(cpu_if->vgic_ap0r[3], 3);
		rmm_vgic_v3_write_ap0rn(cpu_if->vgic_ap0r[2], 2);
		__attribute__((__fallthrough__));
	case 6:
		rmm_vgic_v3_write_ap0rn(cpu_if->vgic_ap0r[1], 1);
		__attribute__((__fallthrough__));
	default:
		rmm_vgic_v3_write_ap0rn(cpu_if->vgic_ap0r[0], 0);
	}

	switch (nr_pre_bits) {
	case 7:
		rmm_vgic_v3_write_ap1rn(cpu_if->vgic_ap1r[3], 3);
		rmm_vgic_v3_write_ap1rn(cpu_if->vgic_ap1r[2], 2);
		__attribute__((__fallthrough__));
	case 6:
		rmm_vgic_v3_write_ap1rn(cpu_if->vgic_ap1r[1], 1);
		__attribute__((__fallthrough__));
	default:
		rmm_vgic_v3_write_ap1rn(cpu_if->vgic_ap1r[0], 0);
	}
}

void rmm_vgic_v3_save_state(vgic_v3_cpu_if_t *cpu_if)
{
	unsigned int nr_lrs = (read_ich_vtr_el2() & 0xf) + 1;
	unsigned int i;
	uint32_t elrsr = read_ich_elrsr_el2();

	cpu_if->vgic_vmcr = read_ich_vmcr_el2();
	/* cpu_if->vgic_misr = read_ich_misr_el2(); */
	cpu_if->vgic_hcr = read_ich_hcr_el2();

	write_ich_hcr_el2(cpu_if->vgic_hcr & ~ICH_HCR_EN);
	for (i = 0; i < nr_lrs; i++) {
		if (elrsr & (1 << i))
			cpu_if->vgic_lr[i] &= ~ICH_LR_STATE;
		else
			cpu_if->vgic_lr[i] = rmm_gic_v3_get_lr(i);

		rmm_gic_v3_set_lr(0, i);
	}

	rmm_vgic_v3_save_aprs(cpu_if);
}

void rmm_vgic_v3_restore_state(vgic_v3_cpu_if_t *cpu_if)
{
	unsigned int nr_lrs = (read_ich_vtr_el2() & 0xf) + 1;
	unsigned int i;

	write_ich_hcr_el2(cpu_if->vgic_hcr);

	for (i = 0; i < nr_lrs; i++) {
		rmm_gic_v3_set_lr(cpu_if->vgic_lr[i], i);
	}

	write_ich_vmcr_el2(cpu_if->vgic_vmcr);
	rmm_vgic_v3_restore_aprs(cpu_if);
}
