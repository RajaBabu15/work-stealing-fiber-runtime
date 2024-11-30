// fiber_switch(void** from_sp, void* to_sp)
//   x0 = &current->saved_sp  (out: current sp written here)
//   x1 = next->saved_sp      (in:  value used directly as new sp)
//
// Fixed 0xb0-byte frame (Boost.Context AAPCS64 layout):
//   d8-d15 @ 0x00, x19-x30 @ 0x40 (x30/LR used as return address via ret)

.text
.p2align 4
.globl _fiber_switch
_fiber_switch:
    sub  sp, sp, #0xb0

    stp  d8,  d9,  [sp, #0x00]
    stp  d10, d11, [sp, #0x10]
    stp  d12, d13, [sp, #0x20]
    stp  d14, d15, [sp, #0x30]

    stp  x19, x20, [sp, #0x40]
    stp  x21, x22, [sp, #0x50]
    stp  x23, x24, [sp, #0x60]
    stp  x25, x26, [sp, #0x70]
    stp  x27, x28, [sp, #0x80]
    stp  x29, x30, [sp, #0x90]

    mov  x9, sp
    str  x9, [x0]
    mov  sp, x1

    ldp  d8,  d9,  [sp, #0x00]
    ldp  d10, d11, [sp, #0x10]
    ldp  d12, d13, [sp, #0x20]
    ldp  d14, d15, [sp, #0x30]

    ldp  x19, x20, [sp, #0x40]
    ldp  x21, x22, [sp, #0x50]
    ldp  x23, x24, [sp, #0x60]
    ldp  x25, x26, [sp, #0x70]
    ldp  x27, x28, [sp, #0x80]
    ldp  x29, x30, [sp, #0x90]

    add  sp, sp, #0xb0
    ret

.subsections_via_symbols