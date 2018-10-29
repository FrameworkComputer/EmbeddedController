/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * __divdi3.S: signed 64 bit division
 */

#define NREGS	 $r6
#define DREGS	 $r8
#define P1H	 $r1
#define P1L	 $r0
#define P2H	 $r3
#define P2L	 $r2
#define NUMHI	 $r7
#define NUMLO	 $r6
#define DENHI	 $r9
#define DENLO	 $r8
#define OFFSET_L 0
#define OFFSET_H 4
#define MHI	 P1H
#define MLO	 P1L
#define W2	 $r3
#define W1	 $r5
#define W0	 $r4
#define T2	 P1L
#define NHI	 P1H
#define NLO	 P1L
#define D	 $r2
#define DLO	 $r3
#define DHI	 $r10
#define Q	 NHI
#define QHI	 W0
#define R	 NLO
#define RHI	 NHI
#define M	 T2
#define M2       DLO

	.text
	.align	2
	.globl	umul_ppmm
	.type	umul_ppmm, @function
	! =====================================================================
	! uint64_t umul_ppmm(uint32_t a, uint32_t b)
	!
	! This function multiplies `a' by `b' to obtain a 64-bit product. The
	! product is broken into two 32-bit pieces which are stored in the zl
	! (low-part at P1L) and zh (high-part at P1H).
	! =====================================================================
umul_ppmm:
	zeh	P2L, $r0	        ! al=a&0xffff
	srli	P2H, $r0, 16	        ! ah=a>>16
	zeh	P1L, $r1	        ! bl=b&0xffff
	srli	P1H, $r1, 16	        ! bh=b>>16
	mul	W1,  P2L, P1H	        ! zA=al*bh
	mul	P2L, P2L, P1L	        ! zl=al*bl
	mul	P1L, P2H, P1L	        ! zB=ah*bl
	add	W1,  W1,  P1L	        ! zA+=zB
	slt	$ta, W1,  P1L	        ! zA<zB
	slli	$ta, $ta, 16	        ! (zA<zB)<<16
	maddr32 $ta, P2H, P1H	        ! zh=ah*bh+((zA<zB)<<16)
	srli	P1H, W1,  16	        ! zA>>16
	add	P1H, P1H, $ta	        ! zh+=(zA>>16)
	slli	P1L, W1,  16	        ! zA<<=16
	add	P1L, P1L, P2L	        ! zl+=zA
	slt	$ta, P1L, P2L	        ! zl<zA
	add	P1H, P1H, $ta	        ! zh+=(zl<zA)
	ret
	.size	umul_ppmm, .-umul_ppmm

	.text
	.align	2
	.type	fudiv_qrnnd, @function
	! =====================================================================
	! uint64_t fudiv_qrnnd(uint64_t n, uint32_t d)
	!
	! This function divides 64-bit numerator n by 32-bit denominator d. The
	! 64-bit return value contains remainder (low-part at P1L) and quotient
	! (high-part at P1H).
	! This function uses a custom calling convention,
	! with register DHI ($r10) call-clobbered instead of callee-saved.
	! =====================================================================
fudiv_qrnnd:
	srli	DHI, D, 16		! d1 = ll_highpart (d)
	zeh	W1,  NLO		! ll_lowpart (n0)
	srli	T2,  NLO, 16		! ll_highpart (n0)
	divr	QHI, RHI, NHI, DHI	! q1 = n1 / __d1, r1 = n1 % __d1
	zeh	DLO, D			! d0 = ll_lowpart (d)
	slli	RHI, RHI, 16		! r1 << 16
	or	RHI, RHI, T2		! __r1 = (__r1 << 16) | ll_highpart(n0)
	mul	M,   QHI, DLO		! m =  __q1*__d0
	slt	$ta, RHI, M		! __r1 < __m
	beqz	$ta, .L2		! if no, skip
	addi	QHI, QHI, -1		! __q1--
	add	RHI, RHI, D		! __r1 += d
	slt	$ta, RHI, D		! __r1 < d
	bnez	$ta, .L2		! if yes, skip
	slt	$ta, RHI, M		! __r1 < __m
	beqz	$ta, .L2		! if no, skip
	addi	QHI, QHI, -1		! __q1--
	add	RHI, RHI, D		! __r1 += d
.L2:
	sub	RHI, RHI, M		! __r1 -= __m
	divr	Q, T2, RHI, DHI		! __q0 = r1 / __d1, __r0 = r1 % __d1
	slli	T2, T2, 16		! __r0 << 16
	or	R, T2, W1		! __r0 = (__r0 << 16) | ll_lowpart(n0)
	mul	M2, DLO, Q		! __m = __q0 * __d0
	slt	$ta, R, M2		! __r0 < __m
	beqz	$ta, .L5		! if no, skip
	add	R, R, D			! __r0 += d
	addi	Q, Q, -1		! __q0--
	slt	$ta, R, D		! __r0 < d
	bnez	$ta, .L5		! if yes, skip
	slt	$ta, R, M2		! __r0 < __m
	beqz	$ta, .L5		! if no, skip
	add	R, R, D			! __r0 += d
	addi	Q, Q, -1		! __q0--

.L5:
	sub	R, R, M2		! r = r0 = __r0 - __m
	slli	QHI, QHI, 16		! __q1 << 16
	or	Q, Q, QHI		! q = (__q1 << 16) | __q0
	ret
	.size	fudiv_qrnnd, .-fudiv_qrnnd

	.align	2
	.globl	__udivmoddi4
	.type	__udivmoddi4, @function
	! =====================================================================
	! uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *r)
	!
	! This function divides 64-bit numerator n by 64-bit denominator d. The
	! quotient is returned as 64-bit return value and the 64-bit remainder
	! is stored at the input address r.
	! stack allocation:
	! sp+40 +------------------+
	!       | q                |
	! sp+32 +------------------+
	!       | bm               |
	! sp+28 +------------------+
	!       | $lp              |
	! sp+24 +------------------+
	!       | $fp              |
	! sp+20 +------------------+
	!       | $r10             |
	! sp+16 +------------------+
	!       | $r6 - $r9        |
	! sp    +------------------+
	! =====================================================================
__udivmoddi4:
	addi	$sp, $sp, -40
	smw.bi	$r6, [$sp], $r10 , 10
	movd44	NREGS, $r0		! (n1,n0)
	movd44	DREGS, $r2		! (d1,d0)
	move	$fp, $r4		! rp
	bnez	P2H, .L9		! if d1 != 0, skip
	slt	$ta, NUMHI, DENLO	! n1 < d0
	beqz	$ta, .L10		! if no, skip
	move	$r0, DENLO
	bal	__clzsi2
	swi	$r0,  [$sp+(28)]	! bm
	beqz	$r0, .LZskipnorm1	! if bm == 0, skip
	sll	DENLO, DENLO, $r0	! d0 <<= bm
	subri	W1, $r0, 32		! 32 - bm
	srl	W1, NUMLO, W1		! n0 >> (32 - bm)
	sll	NUMHI, NUMHI, $r0	! n1 << bm
	or	NUMHI, NUMHI, W1	! n1 =  (n1 << bm) | (n0 >> (32 - bm))
	sll	NUMLO, NUMLO, $r0	! n0 <<= bm
.LZskipnorm1:
	movd44	$r0, NREGS		! (n1,n0)
	move	$r2, DENLO		! d0
	bal	fudiv_qrnnd		! calculate q0 n0
	swi	P1H, [$sp+(32+OFFSET_L)]! q0
	move	NUMLO, P1L		! n0
	move	W1, 0
	swi	W1, [$sp+(32+OFFSET_H)]	! q1 = 0
	b	.L19
.L10:
	beqz	P2L, .LZdivzero		! if d0 != 0, skip
	move	$r0, DENLO
	bal	__clzsi2
	swi	$r0, [$sp+(28)]		! bm
	bnez	$r0, .LZnorm1		! if bm != 0, skip
	sub	NUMHI, NUMHI, DENLO	! n1 -= d0
	movi	W1, 1
	swi	W1, [$sp+(32+OFFSET_H)]	! q1 = 1
	b	.L29

	! to eliminate unaligned branch target
	.align	2
.LZnorm1:
	subri	$ta, $r0, 32		! b = 32 - bm
	sll	DENLO, DENLO, $r0	! d0 <<= bm
	move	$r2, DENLO
	srl	W0, NUMLO, $ta		! n0 >> b
	sll	W1, NUMHI, $r0		! n1 << bm
	sll	NUMLO, NUMLO, $r0	! n0 <<= bm
	or	P1L, W1, W0		! n1 = (n1 << bm) | (n0 >> b)
	srl	P1H, NUMHI, $ta		! n2 = n1 >> b
	bal	fudiv_qrnnd		! caculate q1, n1
	swi	P1H, [$sp+(32+OFFSET_H)]! q1
	move	NUMHI, P1L		! n1
.L29:
	movd44	$r0, NREGS		! (n1,n0)
	move	$r2, DENLO		! d0
	bal	fudiv_qrnnd		! calcuate q0, n0
	swi	P1H, [$sp+(32+OFFSET_L)]
	move	NUMLO, P1L

	! to eliminate unaligned branch target
	.align	2
.L19:
	beqz	$fp, .LZsetq		! if rp == 0, skip
	lwi	W2, [$sp+(28)]		! bm
	movi	NUMHI, 0
	srl	NUMLO, NUMLO, W2	! n0 >> bm
	b	.LZsetr

	! to eliminate unaligned branch target
	.align	2
.LZdivzero:
	! divide-by-zero exception or quotient = 0 and remainder = 0 returned
	divr	NUMHI, NUMLO, DENLO, DENLO
.LZqzero:
	movi	P1H, 0
	movi	P1L, 0
	beqz	$fp, .LZret		! if rp == NULL, skip
	swi	NUMLO, [$fp+OFFSET_L]	! *rp
	swi	NUMHI, [$fp+OFFSET_H]
	b	.LZret
.L9:
	slt	$ta, NUMHI, DENHI	! n1 < d1
	bnez	$ta, .LZqzero		! if yes, skip
	move	$r0, DENHI
	bal	__clzsi2
	swi	$r0, [$sp+(28)]		! bm
	beqz	$r0, .LZskipnorm2	! if bm == 0, skip
	subri	W0, $r0, 32		! b = 32 - bm
	srl	W1, DENLO, W0		! d0 >> b
	sll	$r2, DENHI, $r0		! d1 << bm
	or	$r2, $r2, W1		! d1 = (d0 >> b) | (d1 << bm)
	move	DENHI, $r2
	sll	DENLO, DENLO, $r0	! d0 <<= bm
	srl	W2, NUMLO, W0		! n0 >> b
	sll	NUMLO, NUMLO, $r0	! n0 <<= bm
	sll	P1L, NUMHI, $r0		! n1 << bm
	srl	P1H, NUMHI, W0		! n2 = n1 >> b
	or	P1L, P1L, W2		! n1 = (n0 >> b) | (n1 << bm)
	bal	fudiv_qrnnd		! calculate  q0, n1
	swi	P1H, [$sp+(32+OFFSET_L)]
	move	NUMHI, P1L
	move	P1L, DENLO		! d0
	bal	umul_ppmm
	slt	$ta, NUMHI, MHI		! n1 < m1
	bnez	$ta, .L46		! if yes, skip
	bne	MHI, NUMHI, .L45	! if m1 != n1, skip
	slt	$ta, NUMLO, MLO		! n0 < m0
	beqz	$ta, .L45		! if no, skip
.L46:
	lwi	W2, [$sp+(32+OFFSET_L)]
	sub	MHI, MHI, DENHI		! m1 - d1
	addi	W2, W2, -1		! q0--
	swi	W2, [$sp+(32+OFFSET_L)]
	sub	W2, MLO, DENLO		! __x = m0 - d0
	slt	$ta, MLO, W2		! m0 < __x
	sub	MHI, MHI, $ta		! m1 = m1 - d1 - (__x > m0)
	move	MLO, W2			! m0 = __x
.L45:
	movi	W2, 0
	swi	W2, [$sp+(32+OFFSET_H)]	! q1 = 0
	beqz	$fp, .LZsetq		! if yes, skip
	sub	P1L, NUMLO, MLO		! __x = n0 - m0
	sub	P1H, NUMHI, MHI		! n1 - m1
	slt	$ta, NUMLO, P1L		! n0 < __x
	sub	P1H, P1H, $ta		! n1 = n1 - m1 - (__x > n0)
	lwi	W2, [$sp+(28)]		! bm
	subri	W0, W2, 32		! b
	sll	NUMHI, P1H, W0		! n1 << b
	srl	NUMLO, P1L, W2		! n0 >> bm
	or	NUMLO, NUMLO, NUMHI	! (n1 << b) | (n0 >> bm)
	srl	NUMHI, P1H, W2		! n1 >> bm
.LZsetr:
	swi	NUMLO, [$fp+OFFSET_L]	! remainder
	swi	NUMHI, [$fp+OFFSET_H]
.LZsetq:
	lwi	P1L, [$sp+(32+OFFSET_L)]! quotient
	lwi	P1H, [$sp+(32+OFFSET_H)]

	! to eliminate unaligned branch target
	.align	2
.LZret:
	lmw.bi	$r6, [$sp], $r10 , 10
	addi	$sp, $sp, 40
	ret

.LZskipnorm2:
	move	W2, 0
	slt	$ta, DENHI, NUMHI	! n1 > d1
	bnez	$ta, .L52		! if yes, skip
	slt	$ta, NUMLO, DENLO	! n0 < d0
	bnez	$ta, .L51		! if yes, skip
.L52:
	move	W1, 1
	swi	W1, [$sp+(32+OFFSET_L)]	! q0 = 1
	sub	W0, NUMLO, DENLO	! __x = n0 - d0
	sub	NUMHI, NUMHI, DENHI	! n1 - d1
	slt	$ta, NUMLO, W0		! n0 < __x
	sub	NUMHI, NUMHI, $ta	! n1 = n1 -d1 - (_-x > n0)
	move	NUMLO, W0		! n0 = __x
	b	.L54
.L51:
	swi	W2, [$sp+(32+OFFSET_L)]	! q0 = 0
.L54:
	swi	W2, [$sp+(32+OFFSET_H)]	! q1 = 0
	bnez	$fp, .LZsetr
	b	.LZsetq
	.size	__udivmoddi4, .-__udivmoddi4

	.text
	.align	2
	.globl	__divdi3
	.type	__divdi3, @function
__divdi3:
	! =====================================================================
	! uint64_t __divdi3(uint64_t n, uint64-t d)
	!
	! This function divides n by d and returns the quotient.
	!
	! stack allocation:
	! sp+8  +-----------------------+
	!       | $lp                   |
	! sp+4  +-----------------------+
	!       | $r6                   |
	! sp    +-----------------------+
	! =====================================================================
	smw.adm	$r6, [$sp], $r6, 2

	xor	$r6, P1H, P2H
	srai45	$r6, 31			! signof(numerator xor denominator)
	! abs(denominator)
	bgez	P2H, .L80
	neg	P2H, P2H
	beqz	P2L, .L80
	neg	P2L, P2L
	addi	P2H, P2H, -1

.L80:
	! abs(numerator)
	bgez	P1H, .L81
	neg	P1H, P1H
	beqz	P1L, .L81
	neg	P1L, P1L
	addi	P1H, P1H, -1

.L81:
	! abs(numerator) / abs(denominator)
	movi	$r4, 0			! ignore remainder
	bal	__udivmoddi4
	! numerator / denominator
	beqz	$r6, .L82
	or	$r4, P1H, P1L
	beqz	$r4, .L82
	neg	P1H, P1H
	beqz	P1L, .L82
	neg	P1L, P1L
	addi	P1H, P1H, -1

	! to eliminate unaligned branch target
	.align	2
.L82:
	lmw.bim	$r6, [$sp], $r6, 2
	ret
	.size	__divdi3, .-__divdi3
