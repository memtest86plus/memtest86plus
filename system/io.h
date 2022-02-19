// SPDX-License-Identifier: GPL-2.0
#ifndef IO_H
#define IO_H
/**
 * \file
 *
 * Provides macro definitions for the x86 IO instructions
 * inb/inw/inl/outb/outw/outl and the "string versions" of the same
 * (insb/insw/insl/outsb/outsw/outsl). You can also use "pausing"
 * versions of the single-IO instructions (inb_p/inw_p/..).
 *
 * This file is not meant to be obfuscating: it's just complicated
 * to (a) handle it all in a way that makes gcc able to optimize it
 * as well as possible and (b) trying to avoid writing the same thing
 * over and over again with slight variations and possibly making a
 * mistake somewhere.
 *
 * Derived from memtest86+ io.h.
 * (original contained no copyright statement)
 */

#ifdef SLOW_IO_BY_JUMPING
#define __SLOW_DOWN_IO __asm__ __volatile__("jmp 1f\n1:\tjmp 1f\n1:")
#else
#define __SLOW_DOWN_IO __asm__ __volatile__("outb %al,$0x80")
#endif

#ifdef REALLY_SLOW_IO
#define SLOW_DOWN_IO { __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; }
#else
#define SLOW_DOWN_IO __SLOW_DOWN_IO
#endif

#define __OUT1(s,x) \
static inline void __out##s(unsigned x value, unsigned short port) {

#define __OUT2(s,s1,s2) \
__asm__ __volatile__ ("out" #s " %" s1 "0,%" s2 "1"

#define __OUT(s,s1,x) \
__OUT1(s,x) __OUT2(s,s1,"w") : : "a" (value), "d" (port)); } \
__OUT1(s##c,x) __OUT2(s,s1,"") : : "a" (value), "id" (port)); } \
__OUT1(s##_p,x) __OUT2(s,s1,"w") : : "a" (value), "d" (port)); SLOW_DOWN_IO; } \
__OUT1(s##c_p,x) __OUT2(s,s1,"") : : "a" (value), "id" (port)); SLOW_DOWN_IO; }

#define __IN1(s) \
static inline RETURN_TYPE __in##s(unsigned short port) { RETURN_TYPE _v;

#define __IN2(s,s1,s2) \
__asm__ __volatile__ ("in" #s " %" s2 "1,%" s1 "0"

#define __IN(s,s1,i...) \
__IN1(s) __IN2(s,s1,"w") : "=a" (_v) : "d" (port) ,##i ); return _v; } \
__IN1(s##c) __IN2(s,s1,"") : "=a" (_v) : "id" (port) ,##i ); return _v; } \
__IN1(s##_p) __IN2(s,s1,"w") : "=a" (_v) : "d" (port) ,##i ); SLOW_DOWN_IO; return _v; } \
__IN1(s##c_p) __IN2(s,s1,"") : "=a" (_v) : "id" (port) ,##i ); SLOW_DOWN_IO; return _v; }

#define __OUTS(s) \
static inline void outs##s(unsigned short port, const void * addr, unsigned long count) \
{ __asm__ __volatile__ ("cld ; rep ; outs" #s \
: "=S" (addr), "=c" (count) : "d" (port),"0" (addr),"1" (count)); }

#define RETURN_TYPE unsigned char
/* __IN(b,"b","0" (0)) */
__IN(b,"")
#undef RETURN_TYPE
#define RETURN_TYPE unsigned short
/* __IN(w,"w","0" (0)) */
__IN(w,"")
#undef RETURN_TYPE
#define RETURN_TYPE unsigned int
__IN(l,"")
#undef RETURN_TYPE

__OUT(b,"b",char)
__OUT(w,"w",short)
__OUT(l,,int)

__OUTS(b)
__OUTS(w)
__OUTS(l)

/**
 * Note that due to the way __builtin_constant_p() works, you
 *  - can't use it inside a inline function (it will never be true)
 *  - you don't have to worry about side effects within the __builtin..
 */
#define outb(val,port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
    __outbc((val),(port)) : \
    __outb((val),(port)))

#define inb(port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
    __inbc(port) : \
    __inb(port))


#define outw(val,port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
    __outwc((val),(port)) : \
    __outw((val),(port)))

#define inw(port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
    __inwc(port) : \
    __inw(port))


#define outl(val,port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
    __outlc((val),(port)) : \
    __outl((val),(port)))

#define inl(port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
    __inlc(port) : \
    __inl(port))

static __inline unsigned char
inb_p (unsigned short int __port)
{
    unsigned char _v;

    __asm__ __volatile__ ("\t"
        "inb    %w1,%0      \n\t"
        "outb   %%al,$0x80  \n"
        : "=a" (_v)
        : "Nd" (__port)
    );

    return _v;
}

static __inline void
outb_p (unsigned char __value, unsigned short int __port)
{
    __asm__ __volatile__ ("\t"
        "outb   %b0,%w1     \n\t"
        "outb   %%al,$0x80  \n"
        : /* no outputs */
        : "a" (__value),
          "Nd" (__port)
    );
}

#endif // IO_H
