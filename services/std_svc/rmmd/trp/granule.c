#include <common/debug.h>
#include <services/trp/platform_trp.h>
#include <string.h>
#include <inttypes.h>
#include <platform_def.h>
#include <plat/common/platform.h>
#include <lib/libc/setjmp.h>
#include "trp_private.h"
#include "time.h"

static uint8_t granule_state_table[RMM_NUM_GRANULES/2]; // 4 bits per granule, 2 granule per byte

void rmm_granule_state_table_init (void)
{
	memset(granule_state_table, 0, sizeof(granule_state_table));
}

rmm_granule_state_e rmm_get_granule_state (rmm_addr_t addr)
{
	int granule_number = (addr >> RMM_GRANULE_SIZE2) & (RMM_NUM_GRANULES - 1);
	int index = granule_number >> 1;
	int shift = (granule_number & 1) << 2;
	/*VERBOSE("rmm_get_granule_state 0x%llx. gn=0x%x, statex2=%x\n",
			(unsigned long long)addr, granule_number,
			granule_state_table[index]);*/
	return (granule_state_table[index] >> shift) & 0xf;
}

void rmm_set_granule_state (rmm_addr_t addr, rmm_granule_state_e new_state)
{
	int granule_number = (addr >> RMM_GRANULE_SIZE2) & (RMM_NUM_GRANULES - 1);
	int index = granule_number >> 1;
	/*VERBOSE("rmm_set_granule_state 0x%llx %d. gn=0x%x, old-statex2=%x\n",
			(unsigned long long)addr, new_state, granule_number,
			granule_state_table[index]);*/
	if (granule_number & 1)
		granule_state_table[index] = (new_state << 4) | (granule_state_table[index] & 0xf);
	else
		granule_state_table[index] = (granule_state_table[index] & 0xf0) | new_state;
}

static jmp_buf rmm_jmp_buf[PLATFORM_CORE_COUNT];
static bool rmm_jmp_ready[PLATFORM_CORE_COUNT]; /* rmm_jmp_ready is only for debug purpose */
bool rmm_read_ns (void *rmm_dst, const void *ns_src, size_t size)
{
	assert(((uint64_t)ns_src & (RMM_GRANULE_SIZE - 1UL)) + size <= RMM_GRANULE_SIZE); // cannot read across granule boundary
#ifdef PLAT_fvp
	uint64_t ns_granule = ((uint64_t)ns_src) & ~(RMM_GRANULE_SIZE - 1UL);
	rmm_remap_granule(ns_granule, MT_MEMORY | MT_RO | MT_NS);
#endif
	bool succeed;
	unsigned int core_id = plat_my_core_pos();
	rmm_jmp_ready[core_id] = true;
	if (setjmp(rmm_jmp_buf[core_id])) {
		succeed = false;
	} else {
#ifdef PLAT_qemu
		memcpy(rmm_dst, ns_src + 0x100000000, size);
#elif PLAT_fvp
		memcpy(rmm_dst, ns_src , size);
#endif
		succeed = true;
	}
	rmm_jmp_ready[core_id] = false;
#ifdef PLAT_fvp
	rmm_undo_remap_granule(ns_granule);
#endif
	return succeed;
}

bool rmm_write_ns (void *ns_dst, const void *rmm_src, size_t size)
{
	assert(((uint64_t)ns_dst & (RMM_GRANULE_SIZE - 1UL)) + size <= RMM_GRANULE_SIZE); // cannot read across granule boundary
#ifdef PLAT_fvp
	uint64_t ns_granule = ((uint64_t)ns_dst) & ~(RMM_GRANULE_SIZE - 1UL);
	rmm_remap_granule(ns_granule, MT_MEMORY | MT_RW | MT_NS);
#endif

	bool succeed;
	unsigned int core_id = plat_my_core_pos();
	rmm_jmp_ready[core_id] = true;
	if (setjmp(rmm_jmp_buf[core_id])) {
		succeed = false;
	} else {
#ifdef PLAT_qemu
		memcpy(ns_dst + 0x100000000, rmm_src, size);
#elif PLAT_fvp
		memcpy(ns_dst , rmm_src, size);
#endif
		succeed = true;
	}
	rmm_jmp_ready[core_id] = false;
#ifdef PLAT_fvp
	rmm_undo_remap_granule(ns_granule);
#endif
	return succeed;
}

void rmm_data_abort_handler(void)
{
	uint64_t esr_el2 = read_esr_el2();
	uint64_t far_el2 = read_far_el2();
	uint64_t elr_el2 = read_elr_el2();
	unsigned int core_id = plat_my_core_pos();

	INFO("rmm_data_abort_handler: esr 0x%" PRIx64 " far 0x%" PRIx64 " elr 0x%" PRIx64 "\n",
			esr_el2, far_el2, elr_el2);
	assert(rmm_jmp_ready[core_id]);
	longjmp(rmm_jmp_buf[core_id], 1);
	__builtin_unreachable();
}

static rmm_rtt_entry_state_e rmm_get_rtte_state_internal (uint64_t rtte, int level)
{
	uint64_t outaddr = rtte & RTT_OA_MASK;
	if (rtte & 1) { // valid
		if (!outaddr) {
			ERROR("rmm_get_rtte_state valid but no addr. rtte=%llx, level=%d\n",
					(unsigned long long)rtte, level);
			return ES_UNASSIGNED;
		}
		if ((rtte & 2) && level != RMM_RTT_PAGE_LEVEL)
			return ES_TABLE;
		if (level == RMM_RTT_BLOCK_LEVEL || level == RMM_RTT_PAGE_LEVEL)
			return (rtte & (1UL << RTT_NS_BIT)) ? ES_VALID_NS : ES_VALID;
	} else { // invalid
		if (outaddr)
			return ES_ASSIGNED;
		return (rtte & (1UL << RTT_DESTROYED_BIT)) ? ES_DESTROYED : ES_UNASSIGNED;
	}
	ERROR("rmm_get_rtte_state unknown state. rtte=%llx, level=%d\n",
			(unsigned long long)rtte, level);
	return ES_UNASSIGNED;
}

rmm_rtt_entry_state_e rmm_get_rtte_state (const rmm_rtt_walk_result_t *walk_result)
{
	return rmm_get_rtte_state_internal(*(walk_result->rtte), walk_result->level);
}

uint64_t rmm_get_rtte_outaddr (const rmm_rtt_walk_result_t *walk_result)
{
	return *(walk_result->rtte) & RTT_OA_MASK;
}

/* only for page or block entry */
void rmm_set_rtte (rmm_rtt_walk_result_t *walk_result, uint64_t out_addr, bool valid_bit)
{
	/* AF=1; SH=3; S2AP=3; MEMATTR=inner wb (s2fwb) */
	const uint64_t PAGE_ATTR = (1UL << 10) | (3UL << 8) | (3UL << 6) | (6UL << 2);
	uint64_t ls2b = walk_result->level == RMM_RTT_PAGE_LEVEL ? 2UL : 0UL;
	*(walk_result->rtte) = PAGE_ATTR | (out_addr & RTT_OA_MASK) | ls2b | (valid_bit ? 1UL : 0UL);
}

void rmm_set_rtte_table (rmm_rtt_walk_result_t *walk_result, uint64_t child_rtt)
{
	*(walk_result->rtte) = (child_rtt & RTT_OA_MASK) | 3UL;
}

void rmm_set_rtte_valid (rmm_rtt_walk_result_t *walk_result, bool is_valid)
{
	if (is_valid)
		*(walk_result->rtte) |= 1UL;
	else
		*(walk_result->rtte) &= ~1UL;
}

void rmm_set_rtte_destroyed (rmm_rtt_walk_result_t *walk_result)
{
	*(walk_result->rtte) = 1UL << RTT_DESTROYED_BIT;
}

/* copy MemAttr[2:5], S2AP[6:7], SH[8:9], OA[12:47]
 * set VALID, NS */
void rmm_set_rtte_ns (rmm_rtt_walk_result_t *walk_result, uint64_t ns_rtte)
{
	uint64_t rtte = 1 | (1UL << RTT_NS_BIT); // ns, valid
	rtte |= ns_rtte & 0xfffffffff3fcUL;
	*(walk_result->rtte) = rtte;
}

/* LEVEL_WIDTH: How big does one RTT cover in bits? level 3 covers 21 bits (2MB) */
#define LEVEL_WIDTH(level) (12 + 9 * (4 - (level)))

/* precon:
 *   rtt_level_start <= target_level <= 3
 *   target_addr < 2^ipa_width */
void rmm_rtt_walk (rmm_rtt_walk_result_t *result, const rmm_realm_t *rd, uint64_t target_addr, int target_level)
{
	int curr_level = rd->rtt_level_start;
	/* ARM supports multiple consecutive root page tables.
	 * <root_index> is which one to use. */
	const uint64_t root_index = (target_addr >> LEVEL_WIDTH(curr_level));
	assert(root_index < rd->rtt_num_start);
	uint64_t curr_rtt = rd->rtt_base + (root_index << 12);

	while (1) {
		unsigned int index = (target_addr >> LEVEL_WIDTH(curr_level + 1)) & 0x1ff;
		/*INFO("target_addr=0x%llx, curr_level=%d, root_index=0x%llx, index=%u\n",
				(unsigned long long)target_addr, curr_level, (unsigned long long)root_index, index);*/
		uint64_t rtte = ((uint64_t *)curr_rtt)[index];
		if ((rtte & 3) != 3 || // invalid or block
				curr_level == target_level) { // level reached
			result->rtt_addr = curr_rtt;
			result->rtte = &((uint64_t *)curr_rtt)[index];
			result->level = curr_level;
			return;
		}
		uint64_t out_addr = rtte & RTT_OA_MASK;
		if (!out_addr) {
			ERROR("valid rtte but no outaddr curr_rtt=0x%llx, rtte=0x%llx\n",
					(unsigned long long)curr_rtt, (unsigned long long)rtte);
			return;
		}
		curr_rtt = out_addr;
		curr_level ++;
	}
}

/* Fold the child RTT pointed by parent_rtte.
 * precon:
 *   parent_rtte is ES_TABLE
 * postcon:
 *   parent_rtte address and state is set to the folded result
 *   child RTT state is unchanged. i.e. still GS_RTT */
bool rmm_rtt_fold(rmm_rtt_walk_result_t *parent_rtte)
{
	assert(rmm_get_rtte_state(parent_rtte) == ES_TABLE);
	uint64_t child_rtt = rmm_get_rtte_outaddr(parent_rtte);
	int level = parent_rtte->level + 1;
	rmm_rtt_entry_state_e parent_state = rmm_get_rtte_state_internal(*(uint64_t *)child_rtt, level);
	bool aligned = false;
	uint64_t parent_oa = 0;
	if (level == RMM_RTT_PAGE_LEVEL) {
		parent_oa = *(uint64_t *)child_rtt & RTT_OA_MASK;
		if (rmm_is_addr_rtt_level_aligned(parent_oa, RMM_RTT_BLOCK_LEVEL))
			aligned = true;
	}
	for (int i = 1; i < RMM_GRANULE_SIZE / 8; i ++) {
		uint64_t rtte = ((uint64_t *)child_rtt)[i];
		rmm_rtt_entry_state_e state = rmm_get_rtte_state_internal(rtte, level);
		if (state != parent_state) {
			if (parent_state == ES_UNASSIGNED && state == ES_DESTROYED) {
				parent_state = ES_DESTROYED;
			} else if (parent_state == ES_DESTROYED && state == ES_UNASSIGNED) {
			} else {
				INFO("rmm_rtt_fold not foldable: ps=%d, cs=%d, crtt=0x%llx\n",
						parent_state, state, (unsigned long long)child_rtt);
				return false;
			}
		}
		if (aligned) {
			uint64_t oa = rtte & RTT_OA_MASK;
			if (oa != parent_oa + (i << 12))
				aligned = false;
		}
	}
	if (parent_state == ES_UNASSIGNED) {
		*(parent_rtte->rtte) = 0;
	} else if (parent_state == ES_DESTROYED) {
		*(parent_rtte->rtte) = 1UL << RTT_DESTROYED_BIT;
	} else if (aligned && (parent_state == ES_ASSIGNED ||
				parent_state == ES_VALID ||
				parent_state == ES_VALID_NS)) {
		*(parent_rtte->rtte) = *(uint64_t *)child_rtt;
	} else {
		INFO("rmm_rtt_fold not foldable: ps=%d, aligned=%d, crtt=0x%llx\n",
				parent_state, aligned, (unsigned long long)child_rtt);
		return false;
	}
	return true;
}
