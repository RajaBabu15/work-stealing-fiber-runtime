// fiber_switch(void** from_sp, void* to_sp)
//   x0 = &current->saved_sp  (out: current sp written here)
//   x1 = next->saved_sp      (in:  value used directly as new sp)
//
// Saves x19-x30 onto the current stack (6 stp pairs, 96 bytes),
// writes sp to *x0, loads x1 as the new sp, restores registers, ret.
// On first switch-in, x30 holds &fiber_trampoline (planted by init_fiber_stack).

.text
.p2align 4
.globl _fiber_switch
_fiber_switch:
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x29, x30, [sp, #-16]!
    mov x9, sp
    str x9, [x0]
    mov sp, x1
    ldp x29, x30, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret

.subsections_via_symbols
