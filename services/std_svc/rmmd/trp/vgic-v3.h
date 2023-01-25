#ifndef VGIC_V3_H
#define VGIC_V3_H
#include <stdint.h>
#include <assert.h>

/*
 * rmm vgic only support gicv3.
 */

#define ICH_HCR_EN          (1ULL << 0)
#define ICH_HCR_UIE         (1ULL << 1)
#define ICH_HCR_LRENPIE     (1ULL << 2)
#define ICH_HCR_NPIE        (1ULL << 3)
#define ICH_HCR_VGRP0EIE    (1ULL << 4)
#define ICH_HCR_VGRP0DIE    (1ULL << 5)
#define ICH_HCR_VGRP1EIE    (1ULL << 6)
#define ICH_HCR_VGRP1DIE    (1ULL << 7)
#define ICH_HCR_TC          (1ULL << 10)
#define ICH_HCR_TALL0       (1ULL << 11)
#define ICH_HCR_TALL1       (1ULL << 12)
#define ICH_HCR_TDIR        (1ULL << 14)
#define ICH_HCR_EOIcount_SHIFT  27
#define ICH_HCR_EOIcount_MASK   (0x1fULL << ICH_HCR_EOIcount_SHIFT)

#define REC_ENTRY_ICH_HCR_VALID_MASK \
	(ICH_HCR_TDIR | ICH_HCR_VGRP0DIE | \
	 ICH_HCR_VGRP0EIE |ICH_HCR_VGRP1DIE | \
	 ICH_HCR_VGRP1EIE | ICH_HCR_NPIE | \
	 ICH_HCR_LRENPIE | ICH_HCR_UIE)
#define REC_EXIT_ICH_HCR_VALID_MASK \
	(ICH_HCR_TDIR | ICH_HCR_VGRP0DIE | \
	 ICH_HCR_VGRP0EIE |ICH_HCR_VGRP1DIE | \
	 ICH_HCR_VGRP1EIE | ICH_HCR_NPIE | \
	 ICH_HCR_LRENPIE | ICH_HCR_UIE | ICH_HCR_EOIcount_MASK)

#define ICH_LR_EOI      (1ULL << 41)
#define ICH_LR_GROUP    (1ULL << 60)
#define ICH_LR_HW       (1ULL << 61)
#define ICH_LR_STATE    (3ULL << 62)
#define ICH_LR_PENDING_BIT  (1ULL << 62)
#define ICH_LR_ACTIVE_BIT   (1ULL << 63)
#define ICH_LR_PHYS_ID_SHIFT    32
#define ICH_LR_PHYS_ID_MASK     (0x3ffULL << ICH_LR_PHYS_ID_SHIFT)
#define ICH_LR_PRIOPITY_SHIFT   48
#define ICH_LR_PRIOPITY_MASK    (0xffULL << ICH_LR_PRIOPITY_SHIFT)

#define vtr_to_max_lr_idx(v)    ((v) & 0xf)
#define vtr_to_nr_pre_bits(v)   ((((uint32_t)(v) >> 26) & 7) + 1)
#define vtr_to_nr_apr_regs(v)   (1 << (vtr_to_nr_pre_bits(v) - 5))

typedef struct vgic_v3_cpu_if {
	uint32_t vgic_hcr;
	uint32_t vgic_vmcr;
	uint32_t vgic_ap0r[4];
	uint32_t vgic_ap1r[4];
	uint64_t vgic_lr[16];
} vgic_v3_cpu_if_t;

void rmm_vgic_v3_save_state(vgic_v3_cpu_if_t *cpu_if);
void rmm_vgic_v3_restore_state(vgic_v3_cpu_if_t *cpu_if);

#endif
