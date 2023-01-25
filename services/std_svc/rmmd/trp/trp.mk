#
# Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#

RMM_SOURCES		+=	services/std_svc/rmmd/trp/trp_entry.S	\
				services/std_svc/rmmd/trp/exception.S	\
				services/std_svc/rmmd/trp/trp_main.c \
				services/std_svc/rmmd/trp/time.c \
				services/std_svc/rmmd/trp/granule.c	\
				services/std_svc/rmmd/trp/vgic-v3.c	\
				lib/locks/exclusive/aarch64/spinlock.S \
				lib/el3_runtime/aarch64/context.S

RMM_LINKERFILE		:=	services/std_svc/rmmd/trp/linker.lds

RMM_CPPFLAGS   +=      -DPLAT_XLAT_TABLES_DYNAMIC

# Save & restore during REC_ENTER so that Realm can access timer registers
NS_TIMER_SWITCH     :=  1

# Include the platform-specific TRP Makefile
# If no platform-specific TRP Makefile exists, it means TRP is not supported
# on this platform.
TRP_PLAT_MAKEFILE := $(wildcard ${PLAT_DIR}/trp/trp-${PLAT}.mk)
ifeq (,${TRP_PLAT_MAKEFILE})
  $(error TRP is not supported on platform ${PLAT})
else
  include ${TRP_PLAT_MAKEFILE}
endif
