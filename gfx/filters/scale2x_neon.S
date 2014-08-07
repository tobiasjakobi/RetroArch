@@
@@  Copyright (C) 2012 Roman Pauer
@@
@@  Permission is hereby granted, free of charge, to any person obtaining a copy of
@@  this software and associated documentation files (the "Software"), to deal in
@@  the Software without restriction, including without limitation the rights to
@@  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
@@  of the Software, and to permit persons to whom the Software is furnished to do
@@  so, subject to the following conditions:
@@
@@  The above copyright notice and this permission notice shall be included in all
@@  copies or substantial portions of the Software.
@@
@@  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
@@  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
@@  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
@@  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
@@  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
@@  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
@@  SOFTWARE.
@@

.arm

.include "scale2x_neon.Sinc"

.global neon_scale2x_8_8
.global neon_scale2x_16_16

.align 4
neon_scale2x_8_8:

@ r0     = const uint8_t *src
@ r1     = uint8_t *dst
@ r2     = unsigned int width (pixels)
@ r3     = unsigned int srcstride (bytes)
@ [sp]   = unsigned int dststride (bytes)
@ [sp+4] = unsigned int height (pixels)
@ lr     = return address

        ldr ip, [sp]                    @ ip = dststride
        push {r4-r9}                    @ save r4..r9
        ldr r9, [sp, #(7*4)]            @ r9 = height
        sub r4, r0, r3                  @ r4 = src - srcstride
        add r5, r0, r3                  @ r5 = src + srcstride
        add r6, r1, ip                  @ r6 = dst + dststride
        sub r3, r3, r2                  @ r3 = srcstride - width
        sub ip, ip, r2                  @ ip = dststride - width
        lsl ip, #1                      @ ip = 2 * dststride - 2 * width
        mov r7, r2                      @ r7 = width
        sub r9, r9, #2                  @ r9 = height - 2

@ r0 = src
@ r1 = dst
@ r2 = width
@ r3 = srcdiff (srcstride - width)
@ r4 = src - srcstride
@ r5 = src + srcstride
@ r6 = dst + dststride
@ r7 = counter
@ r8 = tmpreg
@ r9 = height
@ ip = dstdiff (2 * dststride - 2 * width)

    @ first line
        neon_scale2x_8_8_line first, r4, r0, r5, r7, r1, r6, r8, 0, 0

        add r0, r0, r3
        add r4, r4, r3
        add r5, r5, r3
        add r1, r1, ip
        add r6, r6, ip

    @ middle lines
    101:
        mov r7, r2

        neon_scale2x_8_8_line middle, r4, r0, r5, r7, r1, r6, r8, 0, 0

        subS r9, r9, #1
        add r0, r0, r3
        add r4, r4, r3
        add r5, r5, r3
        add r1, r1, ip
        add r6, r6, ip
        bne 101b

    @ last line
        mov r7, r2

        neon_scale2x_8_8_line last, r4, r0, r5, r7, r1, r6, r8, 0, 0

        pop {r4-r9}
        bx lr

@ end procedure neon_scale2x_8_8


neon_scale2x_16_16:

@ r0     = const uint16_t *src
@ r1     = uint16_t *dst
@ r2     = unsigned int width (pixels)
@ r3     = unsigned int srcstride (bytes)
@ [sp]   = unsigned int dststride (bytes)
@ [sp+4] = unsigned int height (pixels)
@ [sp+8] = unsigned int access (bitfield indicating which pixels can be accessed)
@ lr     = return address

        ldr ip, [sp]                    @ ip = dststride
        push {r4-r10}                   @ save r4..r10
        ldr r9,  [sp, #(8*4)]           @ r9 = height
        ldr r10, [sp, #(9*4)]           @ r10 = position
        sub r4, r0, r3                  @ r4 = src - srcstride
        add r5, r0, r3                  @ r5 = src + srcstride
        add r6, r1, ip                  @ r6 = dst + dststride
        sub r3, r3, r2, lsl #1          @ r3 = srcstride - 2 * width
        sub ip, ip, r2, lsl #1          @ ip = dststride - 2 * width
        lsl ip, #1                      @ ip = 2 * dststride - 4 * width
        mov r7, r2                      @ r7 = width

        and r8, r10, #2                 @ check for 'last line' bit (r8 is temp reg)
        cmp r8, #0
        subne r9, r9, #1                @ 'last line' bit set, reduce r9 (width) by 1 (last line)

        and r8, r10, #1                 @ check for 'first line' bit
        cmp r8, #0
        beq 101f                        @ 'first line' bit not set, skip directly to middle lines

@ r0 = src
@ r1 = dst
@ r2 = width
@ r3 = srcdiff (srcstride - 2 * width)
@ r4 = src - srcstride
@ r5 = src + srcstride
@ r6 = dst + dststride
@ r7 = counter
@ r8 = tmpreg
@ r9 = height
@ ip = dstdiff (2 * dststride - 4 * width)

    @ first line
        sub r9, r9, #1                  @ reduce r9 (width) by 1 (first line)

        neon_scale2x_16_16_line first, r4, r0, r5, r7, r1, r6, r8, 0, 0

        add r0, r0, r3
        add r4, r4, r3
        add r5, r5, r3
        add r1, r1, ip
        add r6, r6, ip

    @ middle lines
    101:
        mov r7, r2

        neon_scale2x_16_16_line middle, r4, r0, r5, r7, r1, r6, r8, 0, 0

        subS r9, r9, #1
        add r0, r0, r3
        add r4, r4, r3
        add r5, r5, r3
        add r1, r1, ip
        add r6, r6, ip
        bne 101b

    @ last line
        and r8, r10, #2                 @ check for 'last line' bit
        cmp r8, #0
        beq 102f                        @ 'last line' bit not set, all lines processed

        mov r7, r2

        neon_scale2x_16_16_line last, r4, r0, r5, r7, r1, r6, r8, 0, 0

    @ clean up
    102:
        pop {r4-r10}
        bx lr

@ end procedure neon_scale2x_16_16
