/*
 * Copyright (c) 2022, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <services/rmmd_svc.h>

int plat_get_cca_realm_attest_key(uintptr_t buf, size_t *len, unsigned int type)
{
        assert(type == ATTEST_KEY_CURVE_ECC_SECP384R1);

        if (*len < 0) {
                return -EINVAL;
        }

        //[TODO] Temporarily add invalid value, to implement in future
        (void)memcpy((void *)buf, "0000", 4);
        *len = 4;

        return 0;
}
