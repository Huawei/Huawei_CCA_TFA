#include <arch.h>
#include <asm_macros.S>
#include "trp_private.h"

    .global rmm_vector

.macro unhandled_exception name
	vector_entry \name
    b do_panic
	end_vector_entry \name
.endm

func do_panic
loop:
    wfi
    b loop
endfunc do_panic

vector_base rmm_vector

	/*
	 * Current EL with SP0 : 0x0 - 0x200.
	 */
unhandled_exception sync_exception_sp_el0
unhandled_exception irq_sp_el0
unhandled_exception fiq_sp_el0
unhandled_exception serror_sp_el0

	/*
	 * Current EL with SPx : 0x200 - 0x400.
	 */
vector_entry sync_exception_sp_elx
    b sync_exception_vector_entry
end_vector_entry sync_exception_sp_elx

unhandled_exception irq_sp_elx
unhandled_exception fiq_sp_elx
unhandled_exception serror_sp_elx

	/*
	 * Lower EL using AArch64 : 0x400 - 0x600.
	 */
vector_entry sync_exception_aarch64
    esb
    stp x0, x1, [sp, #-16]
    mov x0, #ARM_EXCEPTION_TRAP
    ldr x1, [sp]
    b __realm_exit
end_vector_entry sync_exception_aarch64

vector_entry irq_aarch64
    esb
    stp x0, x1, [sp, #-16]
    mov x0, #ARM_EXCEPTION_IRQ
    ldr x1, [sp]
    b __realm_exit
end_vector_entry irq_aarch64

vector_entry fiq_aarch64
    esb
    stp x0, x1, [sp, #-16]
    mov x0, #ARM_EXCEPTION_FIQ
    ldr x1, [sp]
    b __realm_exit
end_vector_entry fiq_aarch64

vector_entry serror_aarch64
    esb
    stp x0, x1, [sp, #-16]
    mov x0, #ARM_EXCEPTION_EL1_SERROR
    ldr x1, [sp]
    b __realm_exit
end_vector_entry serror_aarch64

	/*
	 * Lower EL using AArch32 : 0x600 - 0x800.
	 */
unhandled_exception sync_exception_aarch32
unhandled_exception irq_aarch32
unhandled_exception fiq_aarch32
unhandled_exception serror_aarch32

func sync_exception_vector_entry
    mrs x0, esr_el2
    ubfx x0, x0, #ESR_EC_SHIFT, #ESR_EC_LENGTH
    cmp x0, #EC_DABORT_CUR_EL
    b.ne 1f
    bl rmm_data_abort_handler
1:
    b do_panic
endfunc sync_exception_vector_entry

.global __realm_enter

/*
 * uint64_t __realm_enter(rmm_rec_t *rec);
 */
func __realm_enter
    //x0: rec
    sub sp, sp, #112

    # save RMM general purpose registers to stack
    stp x0,  x18, [sp, #16 * 0]
    stp x19, x20, [sp, #16 * 1]
    stp x21, x22, [sp, #16 * 2]
    stp x23, x24, [sp, #16 * 3]
    stp x25, x26, [sp, #16 * 4]
    stp x27, x28, [sp, #16 * 5]
    stp x29, x30, [sp, #16 * 6]

    # load realm general purpose registers from REC
    ldp x2, x3, [x0, #16 * 1]
    ldp x4, x5, [x0, #16 * 2]
    ldp x6, x7, [x0, #16 * 3]
    ldp x8, x9, [x0, #16 * 4]
    ldp x10, x11, [x0, #16 * 5]
    ldp x12, x13, [x0, #16 * 6]
    ldp x14, x15, [x0, #16 * 7]
    ldp x16, x17, [x0, #16 * 8]
    ldp x18, x19, [x0, #16 * 9]
    ldp x20, x21, [x0, #16 * 10]
    ldp x22, x23, [x0, #16 * 11]
    ldp x24, x25, [x0, #16 * 12]
    ldp x26, x27, [x0, #16 * 13]
    ldp x28, x29, [x0, #16 * 14]
    ldr x30, [x0, #16 * 15]
    # load x0 last
    ldp x0, x1, [x0, #16 * 0]

    eret
endfunc __realm_enter

func __realm_exit
    // x0: return code
    // x1: rec
    // x2-x29,lr: rec regs
    // rec x0-x1 on the stack

    # save realm general purpose registers to REC
    stp x2, x3, [x1, #16 * 1]
    stp x4, x5, [x1, #16 * 2]
    stp x6, x7, [x1, #16 * 3]
    stp x8, x9, [x1, #16 * 4]
    stp x10, x11, [x1, #16 * 5]
    stp x12, x13, [x1, #16 * 6]
    stp x14, x15, [x1, #16 * 7]
    stp x16, x17, [x1, #16 * 8]
    stp x18, x19, [x1, #16 * 9]
    stp x20, x21, [x1, #16 * 10]
    stp x22, x23, [x1, #16 * 11]
    stp x24, x25, [x1, #16 * 12]
    stp x26, x27, [x1, #16 * 13]
    stp x28, x29, [x1, #16 * 14]
    str x30, [x1, #16 * 15]

    # save realm's x0-x1
    ldp x2, x3, [sp, #-16]
    stp x2, x3, [x1, #16 * 0]

    # restore RMM general purpose registers from stack
    ldr x18, [sp, #8]
    ldp x19, x20, [sp, #16 * 1]
    ldp x21, x22, [sp, #16 * 2]
    ldp x23, x24, [sp, #16 * 3]
    ldp x25, x26, [sp, #16 * 4]
    ldp x27, x28, [sp, #16 * 5]
    ldp x29, x30, [sp, #16 * 6]
    add sp, sp, #112

    # return as if from __realm_enter
    ret
endfunc __realm_exit
