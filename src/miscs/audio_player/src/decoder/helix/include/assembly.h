/* ***** BEGIN LICENSE BLOCK ***** 
 * Version: RCSL 1.0/RPSL 1.0 
 *  
 * Portions Copyright (c) 1995-2002 RealNetworks, Inc. All Rights Reserved. 
 *      
 * The contents of this file, and the files included with this file, are 
 * subject to the current version of the RealNetworks Public Source License 
 * Version 1.0 (the "RPSL") available at 
 * http://www.helixcommunity.org/content/rpsl unless you have licensed 
 * the file under the RealNetworks Community Source License Version 1.0 
 * (the "RCSL") available at http://www.helixcommunity.org/content/rcsl, 
 * in which case the RCSL will apply. You may also obtain the license terms 
 * directly from RealNetworks.  You may not use this file except in 
 * compliance with the RPSL or, if you have a valid RCSL with RealNetworks 
 * applicable to this file, the RCSL.  Please see the applicable RPSL or 
 * RCSL for the rights, obligations and limitations governing use of the 
 * contents of the file.  
 *  
 * This file is part of the Helix DNA Technology. RealNetworks is the 
 * developer of the Original Code and owns the copyrights in the portions 
 * it created. 
 *  
 * This file, and the files included with this file, is distributed and made 
 * available on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER 
 * EXPRESS OR IMPLIED, AND REALNETWORKS HEREBY DISCLAIMS ALL SUCH WARRANTIES, 
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS 
 * FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. 
 * 
 * Technology Compatibility Kit Test Suite(s) Location: 
 *    http://www.helixcommunity.org/content/tck 
 * 
 * Contributor(s): 
 *  
 * ***** END LICENSE BLOCK ***** */ 

/**************************************************************************************
 * Fixed-point MP3 decoder
 * Jon Recker (jrecker@real.com), Ken Cooke (kenc@real.com)
 * June 2003
 *
 * assembly.h - assembly language functions and prototypes for supported platforms
 *
 * - inline rountines with access to 64-bit multiply results 
 * - x86 (_WIN32) and ARM (ARM_ADS, _WIN32_WCE) versions included
 * - some inline functions are mix of asm and C for speed
 * - some functions are in native asm files, so only the prototype is given here
 *
 * MULSHIFT32(x, y)    signed multiply of two 32-bit integers (x and y), returns top 32 bits of 64-bit result
 * FASTABS(x)          branchless absolute value of signed integer x
 * CLZ(x)              count leading zeros in x
 * MADD64(sum, x, y)   (Windows only) sum [64-bit] += x [32-bit] * y [32-bit]
 * SHL64(sum, x, y)    (Windows only) 64-bit left shift using __int64
 * SAR64(sum, x, y)    (Windows only) 64-bit right shift using __int64
 */

#ifndef _ASSEMBLY_H
#define _ASSEMBLY_H

#if (defined _WIN32 && !defined _WIN32_WCE) || (defined __WINS__ && defined _SYMBIAN) || defined(_OPENWAVE_SIMULATOR) || defined(WINCE_EMULATOR)    /* Symbian emulator for Ix86 */

#pragma warning( disable : 4035 )	/* complains about inline asm not returning a value */

static __inline int MULSHIFT32(int x, int y)	
{
    //    __asm {
    //		mov		eax, x
    //	    imul	y
    //	    mov		eax, edx
    //	}	
    long long temp;	
    temp =  (long long)x * (long long)y;	
    return temp >> 32;
}

static __inline int FASTABS(int x) 
{
	int sign;

	sign = x >> (sizeof(int) * 8 - 1);
	x ^= sign;
	x -= sign;

	return x;
}

static __inline int CLZ(int x)
{
	int numZeros;

	if (!x)
		return (sizeof(int) * 8);

	numZeros = 0;
	while (!(x & 0x80000000)) {
		numZeros++;
		x <<= 1;
	} 

	return numZeros;
}

/* MADD64, SHL64, SAR64:
 * write in assembly to avoid dependency on run-time lib for 64-bit shifts, muls
 *  (sometimes compiler thunks to function calls instead of code generating)
 * required for Symbian emulator
 */
#ifdef __CW32__
typedef long long Word64;
#else
typedef __int64 Word64;
#endif

static __inline Word64 MADD64(Word64 sum, int x, int y)
{
    //	unsigned int sumLo = ((unsigned int *)&sum)[0];
    //	int sumHi = ((int *)&sum)[1]; 
    //	__asm {
    //		mov		eax, x
    //		imul	y
    //		add		eax, sumLo
    //		adc		edx, sumHi
    //	} 	
    return sum + (Word64)x * (Word64)y; 	
    /* equivalent to return (sum + ((__int64)x * y)); */
}

static __inline Word64 SHL64(Word64 x, int n)
{
	// unsigned int xLo = ((unsigned int *)&x)[0];
	// int xHi = ((int *)&x)[1];
	// unsigned char nb = (unsigned char)n;

	// if (n < 32) {
	// 	__asm {
	// 		mov		edx, xHi
	// 		mov		eax, xLo
	// 		mov		cl, nb
	// 		shld    edx, eax, cl
	// 		shl     eax, cl
	// 	}
	// } else if (n < 64) {
	// 	/* shl masks cl to 0x1f */
	// 	__asm {
	// 		mov		edx, xLo
	// 		mov		cl, nb
	// 		xor     eax, eax
	// 		shl     edx, cl
	// 	}
	// } else {
	// 	__asm {
	// 		xor		edx, edx
	// 		xor		eax, eax
	// 	}
	// }
	return x << n;
}

static __inline Word64 SAR64(Word64 x, int n)
{
	// unsigned int xLo = ((unsigned int *)&x)[0];
	// int xHi = ((int *)&x)[1];
	// unsigned char nb = (unsigned char)n;

	// if (n < 32) {
	// 	__asm {
	// 		mov		edx, xHi
	// 		mov		eax, xLo
	// 		mov		cl, nb
	// 		shrd	eax, edx, cl
	// 		sar		edx, cl
	// 	}
	// } else if (n < 64) {
	// 	/* sar masks cl to 0x1f */
	// 	__asm {
	// 		mov		edx, xHi
	// 		mov		eax, xHi
	// 		mov		cl, nb
	// 		sar		edx, 31
	// 		sar		eax, cl
	// 	}
	// } else {
	// 	__asm {
	// 		sar		xHi, 31
	// 		mov		eax, xHi
	// 		mov		edx, xHi
	// 	}
	// }
	return x >> n;
}

#elif (defined _WIN32) && (defined _WIN32_WCE)

/* use asm function for now (EVC++ 3.0 does horrible job compiling __int64 version) */
#define MULSHIFT32	xmp3_MULSHIFT32
int MULSHIFT32(int x, int y);

static __inline int FASTABS(int x) 
{
	int sign;

	sign = x >> (sizeof(int) * 8 - 1);
	x ^= sign;
	x -= sign;

	return x;
}

static __inline int CLZ(int x)
{
	int numZeros;

	if (!x)
		return (sizeof(int) * 8);

	numZeros = 0;
	while (!(x & 0x80000000)) {
		numZeros++;
		x <<= 1;
	} 

	return numZeros;
}

#elif defined ARM_ADS

typedef long long Word64;

#define MULSHIFT32	xmp3_MULSHIFT32
extern int MULSHIFT32(int x, int y);


#define FASTABS	xmp3_FASTABS
int FASTABS(int x);


static __inline int CLZ(int x)
{
	int numZeros;

	if (!x)
		return (sizeof(int) * 8);

	numZeros = 0;
	while (!(x & 0x80000000)) {
		numZeros++;
		x <<= 1;
	} 

	return numZeros;
}

#elif defined(__GNUC__) && defined(__arm__) && (!defined(__thumb__) || defined(__thumb2__))

typedef long long Word64;

/* MULSHIFT32: signed 32x32 multiply, return high 32 bits of 64-bit result.
 * Single SMULL instruction on ARMv7-M/v8-M (replaces the __aeabi_lmul soft
 * multiply path taken by the generic long-long C version).
 *
 * Guarded: SMULL is an ARM-mode instruction on ARMv3M+ but is only encodable in
 * Thumb from ARMv6T2 (Thumb-2) onward. Emit it only when we are in ARM mode or
 * in Thumb-2 mode; Thumb-1 targets (e.g. ARMv5TE built with -mthumb, such as
 * the arm968e-s) hit the generic C fallback below. */
static __inline int MULSHIFT32(int x, int y)
{
	int lo, hi;
	__asm__ ("smull %0, %1, %2, %3"
	         : "=&r"(lo), "=r"(hi)
	         : "r"(x), "r"(y));
	return hi;
}

/* FASTABS: branchless absolute value */
static __inline int FASTABS(int x)
{
	int sign = x >> (sizeof(int) * 8 - 1);
	return (x ^ sign) - sign;
}

/* CLZ: count leading zeros. The ARM CLZ instruction returns 32 for an input
 * of 0, matching the reference C version's contract, so the explicit zero
 * check is no longer needed. */
static __inline int CLZ(int x)
{
	int r;
	__asm__ ("clz %0, %1" : "=r"(r) : "r"(x));
	return r;
}

/* MADD64: 64-bit multiply-accumulate  sum += (int64)x * (int64)y.
 * Expressed in plain C so GCC lowers it to SMULL+SMLAL on ARM. */
static __inline Word64 MADD64(Word64 sum, int x, int y)
{
	return sum + (Word64)x * (Word64)y;
}

/* SHL64 / SAR64: 64-bit shifts via plain C (register-pair shifts on ARM). */
static __inline Word64 SHL64(Word64 x, int n)
{
	return x << n;
}

static __inline Word64 SAR64(Word64 x, int n)
{
	return x >> n;
}

#elif defined(__GNUC__) && defined(__riscv)

/* RISC-V GNU implementation based on ultraembedded/libhelix-mp3 0a0e067. */
typedef long long Word64;

static __inline int MULSHIFT32(int x, int y)
{
	unsigned int result;
	__asm__ volatile ("mulh %0, %1, %2"
	                  : "=r"(result)
	                  : "r"(x), "r"(y));
	return (int)result;
}

static __inline int FASTABS(int x)
{
	int sign = x >> (sizeof(int) * 8 - 1);
	return (x ^ sign) - sign;
}

static __inline int CLZ(int x)
{
	int num_zeros;

	if (!x) {
		return sizeof(int) * 8;
	}

	num_zeros = 0;
	while (!(x & 0x80000000)) {
		num_zeros++;
		x <<= 1;
	}
	return num_zeros;
}

static __inline Word64 MADD64(Word64 sum, int x, int y)
{
	unsigned int result_hi;
	unsigned int result_lo;
	unsigned long long product;

	__asm__ volatile ("mulh %0, %1, %2"
	                  : "=r"(result_hi)
	                  : "r"(x), "r"(y));
	__asm__ volatile ("mul %0, %1, %2"
	                  : "=r"(result_lo)
	                  : "r"(x), "r"(y));

	product = ((unsigned long long)result_hi << 32) | result_lo;
	return sum + (Word64)product;
}

static __inline Word64 SHL64(Word64 x, int n)
{
	return x << n;
}

static __inline Word64 SAR64(Word64 x, int n)
{
	return x >> n;
}

#elif defined(__GNUC__) && defined(__arm__)

/* Generic fallback for ARM GCC targets that lack the SMULL/CLZ instructions in
 * the active instruction set (notably Thumb-1 on ARMv5TE/v6, e.g. arm968e-s
 * built with -mthumb). GCC lowers the long-long expressions to its built-in
 * soft-multiply/shift helpers, producing correct code without inline asm. */
typedef long long Word64;

static __inline int MULSHIFT32(int x, int y)
{
	return (int)(((Word64)x * (Word64)y) >> 32);
}

static __inline int FASTABS(int x)
{
	int sign = x >> (sizeof(int) * 8 - 1);
	return (x ^ sign) - sign;
}

static __inline int CLZ(int x)
{
	int numZeros;

	if (!x)
		return (sizeof(int) * 8);

	numZeros = 0;
	while (!(x & 0x80000000)) {
		numZeros++;
		x <<= 1;
	}

	return numZeros;
}

static __inline Word64 MADD64(Word64 sum, int x, int y)
{
	return sum + (Word64)x * (Word64)y;
}

static __inline Word64 SHL64(Word64 x, int n)
{
	return x << n;
}

static __inline Word64 SAR64(Word64 x, int n)
{
	return x >> n;
}

#else

#error Unsupported platform in assembly.h

#endif	/* platforms */

#endif /* _ASSEMBLY_H */
