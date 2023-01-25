/*
 * Copyright (c) 2022, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

int plat_get_cca_attest_token(uintptr_t buf, size_t *len,
                               uintptr_t hash, size_t hash_size)
{
	if (*len < 8) {
		return -EINVAL;
	}
	//[TODO] Temporarily add invalid value, to implement in future
	memset((void *)buf, 0, 8);
	*len = 8;

	return 0;
}
