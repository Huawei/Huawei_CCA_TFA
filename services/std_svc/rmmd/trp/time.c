#include <inttypes.h>
#include <plat/common/platform.h>

uint64_t get_timer_value_us(uint64_t start)
{
	uint64_t cntpct;

	isb();
	cntpct = read_cntpct_el0();
	return (cntpct * 1000000ULL / read_cntfrq_el0() - start);
}