#ifndef __RMM_RMI_H
#define __RMM_RMI_H
#include <stdint.h>

typedef struct rmi_realm_params {
	uint64_t par_base;
	uint64_t par_size;
	uint64_t rtt_base;
	uint64_t measurement_algo;
	uint64_t features_0;
	int64_t rtt_level_start;
	uint32_t rtt_num_start;
	uint32_t vmid;
} rmi_realm_params_t;

typedef struct rmi_rec_params {
	uint64_t gprs[8];
	uint64_t pc;
	uint64_t flags;
	uint64_t aux[16];
} rmi_rec_params_t;

#define RMI_DISPOSE_ACCEPT 0
#define RMI_DISPOSE_REJECT 1

typedef struct rmi_rec_entry {
	uint64_t gprs[7];
	uint64_t is_emulated_mmio;
	uint64_t emulated_read_value;
	uint64_t dispose_response;
	uint64_t gicv3_lrs[16];
	uint64_t gicv3_hcr;
} rmi_rec_entry_t;

#define RMI_EXIT_SYNC    0
#define RMI_EXIT_IRQ     1
#define RMI_EXIT_FIQ     2
#define RMI_EXIT_PSCI    3
#define RMI_EXIT_DISPOSE 4

typedef struct rmi_rec_exit {
	uint64_t reason;
	uint64_t esr;
	uint64_t far_;
	uint64_t hpfar;
	uint64_t emulated_write_value;
	uint64_t gprs[7];
	uint64_t dispose_base;
	uint64_t dispose_size;
	uint64_t gicv3_vmcr;
	uint64_t gicv3_misr;
	uint64_t cntv_ctl;
	uint64_t cntv_cval;
	uint64_t cntp_ctl;
	uint64_t cntp_cval;
	uint64_t gicv3_lrs[16];
	uint64_t gicv3_hcr;
} rmi_rec_exit_t;

typedef struct rmi_rec_run {
	rmi_rec_entry_t entry;
	rmi_rec_exit_t exit;
} rmi_rec_run_t;

#endif
