#ifndef __RMM_TIME_H
#define __RMM_TIME_H
#include <stdint.h>

uint64_t get_timer_value_us(uint64_t start);

#define MEASURE_TIME_START(mark)                    \
    uint64_t start_time_##mark = get_timer_value_us(0);

#define MEASURE_TIME_STOP(mark)                    \
    uint64_t elapse_time_##mark = get_timer_value_us(start_time_##mark); \
	printf("Function: %s  measure time interval(%d): %ld.%ld MS\n", __func__, mark, elapse_time_##mark/1000, elapse_time_##mark%1000);


#endif
