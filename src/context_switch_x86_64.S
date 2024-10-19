// fiber_switch(void** from_sp, void* to_sp)
//   rdi = &current->saved_sp  (out)
//   rsi = next->saved_sp      (in, used as new rsp directly)
//
// Saves rbx,rbp,r12-r15 onto the current stack, writes rsp to *rdi,
// loads rsi as the new rsp, restores registers, ret.
// On first switch-in the return address slot holds &fiber_trampoline.

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
    movq  %rsp, (%rdi)
    movq  %rsi, %rsp
    popq  %r15
    popq  %r14
    popq  %r13
    popq  %r12
    popq  %rbp
    popq  %rbx
    ret

.subsections_via_symbols
