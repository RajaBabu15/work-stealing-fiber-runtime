// fiber_switch(void** from_sp, void* to_sp)
//   rdi = &current->saved_sp  (out)
//   rsi = next->saved_sp      (in, used as new rsp directly)
//
// Saves/restores SysV callee-saved GPRs (rbx, rbp, r12-r15) and xmm6-xmm15.

.text
.p2align 4
.globl _fiber_switch
_fiber_switch:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq  $160, %rsp

    movups %xmm6,  0(%rsp)
    movups %xmm7,  16(%rsp)
    movups %xmm8,  32(%rsp)
    movups %xmm9,  48(%rsp)
    movups %xmm10, 64(%rsp)
    movups %xmm11, 80(%rsp)
    movups %xmm12, 96(%rsp)
    movups %xmm13, 112(%rsp)
    movups %xmm14, 128(%rsp)
    movups %xmm15, 144(%rsp)

    movq  %rsp, (%rdi)
    movq  %rsi, %rsp

    movups 144(%rsp), %xmm15
    movups 128(%rsp), %xmm14
    movups 112(%rsp), %xmm13
    movups 96(%rsp),  %xmm12
    movups 80(%rsp),  %xmm11
    movups 64(%rsp),  %xmm10
    movups 48(%rsp),  %xmm9
    movups 32(%rsp),  %xmm8
    movups 16(%rsp),  %xmm7
    movups 0(%rsp),   %xmm6
    addq  $160, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.subsections_via_symbols