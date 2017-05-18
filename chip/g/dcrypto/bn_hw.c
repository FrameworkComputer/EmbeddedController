/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "dcrypto.h"
#include "internal.h"
#include "registers.h"
#include "trng.h"

/* Firmware blob for crypto accelerator */

/* AUTO-GENERATED.  DO NOT MODIFY. */
const uint32_t IMEM_dcrypto[] = {
/* @0x0: function vectors[15] { */
#define CF_vectors_adr 0
0x10080010,	/* b p256init */
0x1008000f,	/* b __notused */
0x1008004f,	/* b p256isoncurve */
0x10080237,	/* b p256scalarmult */
0x100801a8,	/* b p256scalarbasemult */
0x10080181,	/* b p256sign */
0x100801e2,	/* b p256verify */
0x1008000f,	/* b __notused */
0x1008000f,	/* b __notused */
0x1008000f,	/* b __notused */
0x10080323,	/* b modexp */
0x100802fe,	/* b mul1 */
0x100802cd,	/* b mulx */
0x1008034d,	/* b modload */
0x0c000000,	/* ret */
/* } */
/* @0xf: function __notused[1] { */
#define CF___notused_adr 15
0x0c000000,	/* ret */
/* } */
/* @0x10: function p256init[25] { */
#define CF_p256init_adr 16
0xfc000000,	/* nop */
0x4c7fff00,	/* xor r31, r31, r31 */
0x4c7bde00,	/* xor r30, r30, r30 */
0x80780001,	/* movi r30.0l, #1 */
0x847421c0,	/* ldci r29, [#14] */
0x847021e0,	/* ldci r28, [#15] */
0x98801d00,	/* ldmod r29 */
0x7c6c1f00,	/* mov r27, r31 */
0x83ed5ac6,	/* movi r27.7h, #23238 */
0x83ec35d8,	/* movi r27.7l, #13784 */
0x836daa3a,	/* movi r27.6h, #43578 */
0x836c93e7,	/* movi r27.6l, #37863 */
0x82edb3eb,	/* movi r27.5h, #46059 */
0x82ecbd55,	/* movi r27.5l, #48469 */
0x826d7698,	/* movi r27.4h, #30360 */
0x826c86bc,	/* movi r27.4l, #34492 */
0x81ed651d,	/* movi r27.3h, #25885 */
0x81ec06b0,	/* movi r27.3l, #1712 */
0x816dcc53,	/* movi r27.2h, #52307 */
0x816cb0f6,	/* movi r27.2l, #45302 */
0x80ed3bce,	/* movi r27.1h, #15310 */
0x80ec3c3e,	/* movi r27.1l, #15422 */
0x806d27d2,	/* movi r27.0h, #10194 */
0x806c604b,	/* movi r27.0l, #24651 */
0x0c000000,	/* ret */
/* } */
/* @0x29: function MulMod[38] { */
#define CF_MulMod_adr 41
0x584f3800,	/* mul128 r19, r24l, r25l */
0x59d33800,	/* mul128 r20, r24u, r25u */
0x58d73800,	/* mul128 r21, r24u, r25l */
0x504eb310,	/* add r19, r19, r21 << 128 */
0x50d2b490,	/* addc r20, r20, r21 >> 128 */
0x59573800,	/* mul128 r21, r24l, r25u */
0x504eb310,	/* add r19, r19, r21 << 128 */
0x50d2b490,	/* addc r20, r20, r21 >> 128 */
0x645bfc02,	/* selm r22, r28, r31 */
0x685693ff,	/* rshi r21, r19, r20 >> 255 */
0x585f9500,	/* mul128 r23, r21l, r28l */
0x59e39500,	/* mul128 r24, r21u, r28u */
0x58e79500,	/* mul128 r25, r21u, r28l */
0x505f3710,	/* add r23, r23, r25 << 128 */
0x50e33890,	/* addc r24, r24, r25 >> 128 */
0x59679500,	/* mul128 r25, r21l, r28u */
0x505f3710,	/* add r23, r23, r25 << 128 */
0x50e33890,	/* addc r24, r24, r25 >> 128 */
0x6867f4ff,	/* rshi r25, r20, r31 >> 255 */
0x5062b800,	/* add r24, r24, r21 */
0x50e7f900,	/* addc r25, r25, r31 */
0x5062d800,	/* add r24, r24, r22 */
0x50e7f900,	/* addc r25, r25, r31 */
0x68573801,	/* rshi r21, r24, r25 >> 1 */
0x585abd00,	/* mul128 r22, r29l, r21l */
0x59debd00,	/* mul128 r23, r29u, r21u */
0x58e2bd00,	/* mul128 r24, r29u, r21l */
0x505b1610,	/* add r22, r22, r24 << 128 */
0x50df1790,	/* addc r23, r23, r24 >> 128 */
0x5962bd00,	/* mul128 r24, r29l, r21u */
0x505b1610,	/* add r22, r22, r24 << 128 */
0x50df1790,	/* addc r23, r23, r24 >> 128 */
0x545ad300,	/* sub r22, r19, r22 */
0x54d2f400,	/* subb r20, r20, r23 */
0x6457fd01,	/* sell r21, r29, r31 */
0x5456b600,	/* sub r21, r22, r21 */
0x9c4ff500,	/* addm r19, r21, r31 */
0x0c000000,	/* ret */
/* } */
/* @0x4f: function p256isoncurve[24] { */
#define CF_p256isoncurve_adr 79
0x84004000,	/* ldi r0, [#0] */
0x95800000,	/* lddmp r0 */
0x82800018,	/* movi r0.5l, #24 */
0x83000018,	/* movi r0.6l, #24 */
0x80000000,	/* movi r0.0l, #0 */
0x97800000,	/* ldrfp r0 */
0x8c181600,	/* ld *6, *6 */
0x7c641800,	/* mov r25, r24 */
0x08000029,	/* call &MulMod */
0x7c001300,	/* mov r0, r19 */
0x8c141500,	/* ld *5, *5 */
0x7c641800,	/* mov r25, r24 */
0x08000029,	/* call &MulMod */
0x8c141500,	/* ld *5, *5 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0x8c141500,	/* ld *5, *5 */
0xa04f1300,	/* subm r19, r19, r24 */
0xa04f1300,	/* subm r19, r19, r24 */
0xa04f1300,	/* subm r19, r19, r24 */
0x9c637300,	/* addm r24, r19, r27 */
0x904c0500,	/* st *5, *3 */
0x90500000,	/* st *0, *4 */
0x0c000000,	/* ret */
/* } */
/* @0x67: function ProjAdd[80] { */
#define CF_ProjAdd_adr 103
0x7c600b00,	/* mov r24, r11 */
0x7c640800,	/* mov r25, r8 */
0x08000029,	/* call &MulMod */
0x7c381300,	/* mov r14, r19 */
0x7c600c00,	/* mov r24, r12 */
0x7c640900,	/* mov r25, r9 */
0x08000029,	/* call &MulMod */
0x7c3c1300,	/* mov r15, r19 */
0x7c600d00,	/* mov r24, r13 */
0x7c640a00,	/* mov r25, r10 */
0x08000029,	/* call &MulMod */
0x7c401300,	/* mov r16, r19 */
0x9c458b00,	/* addm r17, r11, r12 */
0x9c492800,	/* addm r18, r8, r9 */
0x7c601100,	/* mov r24, r17 */
0x7c641200,	/* mov r25, r18 */
0x08000029,	/* call &MulMod */
0x9c49ee00,	/* addm r18, r14, r15 */
0xa0465300,	/* subm r17, r19, r18 */
0x9c49ac00,	/* addm r18, r12, r13 */
0x9c4d4900,	/* addm r19, r9, r10 */
0x7c601200,	/* mov r24, r18 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0x7c481300,	/* mov r18, r19 */
0x9c4e0f00,	/* addm r19, r15, r16 */
0xa04a7200,	/* subm r18, r18, r19 */
0x9c4dab00,	/* addm r19, r11, r13 */
0x9c314800,	/* addm r12, r8, r10 */
0x7c601300,	/* mov r24, r19 */
0x7c640c00,	/* mov r25, r12 */
0x08000029,	/* call &MulMod */
0x7c2c1300,	/* mov r11, r19 */
0x9c320e00,	/* addm r12, r14, r16 */
0xa0318b00,	/* subm r12, r11, r12 */
0x7c601b00,	/* mov r24, r27 */
0x7c641000,	/* mov r25, r16 */
0x08000029,	/* call &MulMod */
0xa02e6c00,	/* subm r11, r12, r19 */
0x9c356b00,	/* addm r13, r11, r11 */
0x9c2dab00,	/* addm r11, r11, r13 */
0xa0356f00,	/* subm r13, r15, r11 */
0x9c2d6f00,	/* addm r11, r15, r11 */
0x7c601b00,	/* mov r24, r27 */
0x7c640c00,	/* mov r25, r12 */
0x08000029,	/* call &MulMod */
0x9c3e1000,	/* addm r15, r16, r16 */
0x9c420f00,	/* addm r16, r15, r16 */
0xa0321300,	/* subm r12, r19, r16 */
0xa031cc00,	/* subm r12, r12, r14 */
0x9c3d8c00,	/* addm r15, r12, r12 */
0x9c318f00,	/* addm r12, r15, r12 */
0x9c3dce00,	/* addm r15, r14, r14 */
0x9c39cf00,	/* addm r14, r15, r14 */
0xa03a0e00,	/* subm r14, r14, r16 */
0x7c601200,	/* mov r24, r18 */
0x7c640c00,	/* mov r25, r12 */
0x08000029,	/* call &MulMod */
0x7c3c1300,	/* mov r15, r19 */
0x7c600e00,	/* mov r24, r14 */
0x7c640c00,	/* mov r25, r12 */
0x08000029,	/* call &MulMod */
0x7c401300,	/* mov r16, r19 */
0x7c600b00,	/* mov r24, r11 */
0x7c640d00,	/* mov r25, r13 */
0x08000029,	/* call &MulMod */
0x9c321300,	/* addm r12, r19, r16 */
0x7c601100,	/* mov r24, r17 */
0x7c640b00,	/* mov r25, r11 */
0x08000029,	/* call &MulMod */
0xa02df300,	/* subm r11, r19, r15 */
0x7c601200,	/* mov r24, r18 */
0x7c640d00,	/* mov r25, r13 */
0x08000029,	/* call &MulMod */
0x7c341300,	/* mov r13, r19 */
0x7c601100,	/* mov r24, r17 */
0x7c640e00,	/* mov r25, r14 */
0x08000029,	/* call &MulMod */
0x9c366d00,	/* addm r13, r13, r19 */
0x0c000000,	/* ret */
/* } */
/* @0xb7: function ProjToAffine[116] { */
#define CF_ProjToAffine_adr 183
0x9c2bea00,	/* addm r10, r10, r31 */
0x7c600a00,	/* mov r24, r10 */
0x7c640a00,	/* mov r25, r10 */
0x08000029,	/* call &MulMod */
0x7c601300,	/* mov r24, r19 */
0x7c640a00,	/* mov r25, r10 */
0x08000029,	/* call &MulMod */
0x7c301300,	/* mov r12, r19 */
0x7c601300,	/* mov r24, r19 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0x7c601300,	/* mov r24, r19 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0x7c601300,	/* mov r24, r19 */
0x7c640c00,	/* mov r25, r12 */
0x08000029,	/* call &MulMod */
0x7c341300,	/* mov r13, r19 */
0x05004004,	/* loop #4 ( */
0x7c601300,	/* mov r24, r19 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0xfc000000,	/* nop */
/*		   ) */
0x7c601300,	/* mov r24, r19 */
0x7c640d00,	/* mov r25, r13 */
0x08000029,	/* call &MulMod */
0x7c381300,	/* mov r14, r19 */
0x05008004,	/* loop #8 ( */
0x7c601300,	/* mov r24, r19 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0xfc000000,	/* nop */
/*		   ) */
0x7c601300,	/* mov r24, r19 */
0x7c640e00,	/* mov r25, r14 */
0x08000029,	/* call &MulMod */
0x7c3c1300,	/* mov r15, r19 */
0x05010004,	/* loop #16 ( */
0x7c601300,	/* mov r24, r19 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0xfc000000,	/* nop */
/*		   ) */
0x7c601300,	/* mov r24, r19 */
0x7c640f00,	/* mov r25, r15 */
0x08000029,	/* call &MulMod */
0x7c401300,	/* mov r16, r19 */
0x05020004,	/* loop #32 ( */
0x7c601300,	/* mov r24, r19 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0xfc000000,	/* nop */
/*		   ) */
0x7c441300,	/* mov r17, r19 */
0x7c600a00,	/* mov r24, r10 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0x050c0004,	/* loop #192 ( */
0x7c601300,	/* mov r24, r19 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0xfc000000,	/* nop */
/*		   ) */
0x7c481300,	/* mov r18, r19 */
0x7c601100,	/* mov r24, r17 */
0x7c641000,	/* mov r25, r16 */
0x08000029,	/* call &MulMod */
0x05010004,	/* loop #16 ( */
0x7c601300,	/* mov r24, r19 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0xfc000000,	/* nop */
/*		   ) */
0x7c600f00,	/* mov r24, r15 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0x05008004,	/* loop #8 ( */
0x7c601300,	/* mov r24, r19 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0xfc000000,	/* nop */
/*		   ) */
0x7c600e00,	/* mov r24, r14 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0x05004004,	/* loop #4 ( */
0x7c601300,	/* mov r24, r19 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0xfc000000,	/* nop */
/*		   ) */
0x7c600d00,	/* mov r24, r13 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0x05002004,	/* loop #2 ( */
0x7c601300,	/* mov r24, r19 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0xfc000000,	/* nop */
/*		   ) */
0x7c600c00,	/* mov r24, r12 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0x05002004,	/* loop #2 ( */
0x7c601300,	/* mov r24, r19 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0xfc000000,	/* nop */
/*		   ) */
0x7c600a00,	/* mov r24, r10 */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0x7c601300,	/* mov r24, r19 */
0x7c641200,	/* mov r25, r18 */
0x08000029,	/* call &MulMod */
0x7c381300,	/* mov r14, r19 */
0x7c600800,	/* mov r24, r8 */
0x7c640e00,	/* mov r25, r14 */
0x08000029,	/* call &MulMod */
0x7c2c1300,	/* mov r11, r19 */
0x7c600900,	/* mov r24, r9 */
0x7c640e00,	/* mov r25, r14 */
0x08000029,	/* call &MulMod */
0x7c301300,	/* mov r12, r19 */
0x0c000000,	/* ret */
/* } */
/* @0x12b: function ModInv[17] { */
#define CF_ModInv_adr 299
0x98080000,	/* stmod r2 */
0x55080202,	/* subi r2, r2, #2 */
0x7c041e00,	/* mov r1, r30 */
0x0510000c,	/* loop #256 ( */
0x7c600100,	/* mov r24, r1 */
0x7c640100,	/* mov r25, r1 */
0x08000029,	/* call &MulMod */
0x7c0c1300,	/* mov r3, r19 */
0x50084200,	/* add r2, r2, r2 */
0x64046108,	/* selc r1, r1, r3 */
0x1008813a,	/* bnc nomul */
0x7c600300,	/* mov r24, r3 */
0x7c640000,	/* mov r25, r0 */
0x08000029,	/* call &MulMod */
0x7c041300,	/* mov r1, r19 */
/*nomul: */
0xfc000000,	/* nop */
/*		   ) */
0x0c000000,	/* ret */
/* } */
/* @0x13c: function FetchBandRandomize[11] { */
#define CF_FetchBandRandomize_adr 316
0x99080000,	/* strnd r2 */
0x9c6be200,	/* addm r26, r2, r31 */
0x8c081500,	/* ld *2, *5 */
0x7c641a00,	/* mov r25, r26 */
0x08000029,	/* call &MulMod */
0x7c181300,	/* mov r6, r19 */
0x8c081600,	/* ld *2, *6 */
0x7c641a00,	/* mov r25, r26 */
0x08000029,	/* call &MulMod */
0x7c1c1300,	/* mov r7, r19 */
0x0c000000,	/* ret */
/* } */
/* @0x147: function ProjDouble[5] { */
#define CF_ProjDouble_adr 327
0x7c2c0800,	/* mov r11, r8 */
0x7c300900,	/* mov r12, r9 */
0x7c340a00,	/* mov r13, r10 */
0x08000067,	/* call &ProjAdd */
0x0c000000,	/* ret */
/* } */
/* @0x14c: function ScalarMult_internal[53] { */
#define CF_ScalarMult_internal_adr 332
0x84742200,	/* ldci r29, [#16] */
0x98801d00,	/* ldmod r29 */
0x8c041100,	/* ld *1, *1 */
0x9c07e100,	/* addm r1, r1, r31 */
0xa0002000,	/* subm r0, r0, r1 */
0x847421c0,	/* ldci r29, [#14] */
0x98801d00,	/* ldmod r29 */
0x0800013c,	/* call &FetchBandRandomize */
0x7c200600,	/* mov r8, r6 */
0x7c240700,	/* mov r9, r7 */
0x7c281a00,	/* mov r10, r26 */
0x08000147,	/* call &ProjDouble */
0x7c0c0b00,	/* mov r3, r11 */
0x7c100c00,	/* mov r4, r12 */
0x7c140d00,	/* mov r5, r13 */
0x7c201f00,	/* mov r8, r31 */
0x7c241e00,	/* mov r9, r30 */
0x7c281f00,	/* mov r10, r31 */
0x05100020,	/* loop #256 ( */
0x08000147,	/* call &ProjDouble */
0x0800013c,	/* call &FetchBandRandomize */
0x4c202000,	/* xor r8, r0, r1 */
0x64206602,	/* selm r8, r6, r3 */
0x64248702,	/* selm r9, r7, r4 */
0x6428ba02,	/* selm r10, r26, r5 */
0x7c080b00,	/* mov r2, r11 */
0x7c180c00,	/* mov r6, r12 */
0x7c1c0d00,	/* mov r7, r13 */
0x08000067,	/* call &ProjAdd */
0x44202000,	/* or r8, r0, r1 */
0x64204b02,	/* selm r8, r11, r2 */
0x6424cc02,	/* selm r9, r12, r6 */
0x6428ed02,	/* selm r10, r13, r7 */
0x680000ff,	/* rshi r0, r0, r0 >> 255 */
0x680421ff,	/* rshi r1, r1, r1 >> 255 */
0x992c0000,	/* strnd r11 */
0x99300000,	/* strnd r12 */
0x99340000,	/* strnd r13 */
0x99080000,	/* strnd r2 */
0x7c600300,	/* mov r24, r3 */
0x7c640200,	/* mov r25, r2 */
0x08000029,	/* call &MulMod */
0x7c0c1300,	/* mov r3, r19 */
0x7c600400,	/* mov r24, r4 */
0x7c640200,	/* mov r25, r2 */
0x08000029,	/* call &MulMod */
0x7c101300,	/* mov r4, r19 */
0x7c600500,	/* mov r24, r5 */
0x7c640200,	/* mov r25, r2 */
0x08000029,	/* call &MulMod */
0x7c141300,	/* mov r5, r19 */
/*		   ) */
0x080000b7,	/* call &ProjToAffine */
0x0c000000,	/* ret */
/* } */
/* @0x181: function p256sign[39] { */
#define CF_p256sign_adr 385
0xfc000000,	/* nop */
0x84004000,	/* ldi r0, [#0] */
0x95800000,	/* lddmp r0 */
0x80000000,	/* movi r0.0l, #0 */
0x80800001,	/* movi r0.1l, #1 */
0x81000018,	/* movi r0.2l, #24 */
0x97800000,	/* ldrfp r0 */
0x84002300,	/* ldci r0, [#24] */
0x90540000,	/* st *0, *5 */
0xfc000000,	/* nop */
0x84002320,	/* ldci r0, [#25] */
0x90580000,	/* st *0, *6 */
0xfc000000,	/* nop */
0x8c001000,	/* ld *0, *0 */
0x0800014c,	/* call &ScalarMult_internal */
0x84742200,	/* ldci r29, [#16] */
0x84702220,	/* ldci r28, [#17] */
0x98801d00,	/* ldmod r29 */
0x8c001000,	/* ld *0, *0 */
0x0800012b,	/* call &ModInv */
0x8c081700,	/* ld *2, *7 */
0x7c640100,	/* mov r25, r1 */
0x08000029,	/* call &MulMod */
0x9c63eb00,	/* addm r24, r11, r31 */
0x904c0200,	/* st *2, *3 */
0xfc000000,	/* nop */
0x7c641300,	/* mov r25, r19 */
0x08000029,	/* call &MulMod */
0x7c001300,	/* mov r0, r19 */
0x8c081200,	/* ld *2, *2 */
0x7c640100,	/* mov r25, r1 */
0x08000029,	/* call &MulMod */
0x9c001300,	/* addm r0, r19, r0 */
0x90500000,	/* st *0, *4 */
0xfc000000,	/* nop */
0x847421c0,	/* ldci r29, [#14] */
0x847021e0,	/* ldci r28, [#15] */
0x98801d00,	/* ldmod r29 */
0x0c000000,	/* ret */
/* } */
/* @0x1a8: function p256scalarbasemult[21] { */
#define CF_p256scalarbasemult_adr 424
0xfc000000,	/* nop */
0x84004000,	/* ldi r0, [#0] */
0x95800000,	/* lddmp r0 */
0x80000000,	/* movi r0.0l, #0 */
0x80800001,	/* movi r0.1l, #1 */
0x81000018,	/* movi r0.2l, #24 */
0x8180000b,	/* movi r0.3l, #11 */
0x97800000,	/* ldrfp r0 */
0x8c001100,	/* ld *0, *1 */
0x99800000,	/* ldrnd r0 */
0x84002300,	/* ldci r0, [#24] */
0x90540000,	/* st *0, *5 */
0xfc000000,	/* nop */
0x84002320,	/* ldci r0, [#25] */
0x90580000,	/* st *0, *6 */
0xfc000000,	/* nop */
0x8c001700,	/* ld *0, *7 */
0x0800014c,	/* call &ScalarMult_internal */
0x90540b00,	/* st *3++, *5 */
0x90580b00,	/* st *3++, *6 */
0x0c000000,	/* ret */
/* } */
/* @0x1bd: function ModInvVar[37] { */
#define CF_ModInvVar_adr 445
0x7c081f00,	/* mov r2, r31 */
0x7c0c1e00,	/* mov r3, r30 */
0x98100000,	/* stmod r4 */
0x981c0000,	/* stmod r7 */
0x7c140000,	/* mov r5, r0 */
/*impvt_Loop: */
0x44108400,	/* or r4, r4, r4 */
0x100011cd,	/* bl impvt_Uodd */
0x6813e401,	/* rshi r4, r4, r31 >> 1 */
0x44084200,	/* or r2, r2, r2 */
0x100011c9,	/* bl impvt_Rodd */
0x680be201,	/* rshi r2, r2, r31 >> 1 */
0x100801c2,	/* b impvt_Loop */
/*impvt_Rodd: */
0x50084700,	/* add r2, r7, r2 */
0x509bff00,	/* addc r6, r31, r31 */
0x6808c201,	/* rshi r2, r2, r6 >> 1 */
0x100801c2,	/* b impvt_Loop */
/*impvt_Uodd: */
0x4414a500,	/* or r5, r5, r5 */
0x100011d8,	/* bl impvt_UVodd */
0x6817e501,	/* rshi r5, r5, r31 >> 1 */
0x440c6300,	/* or r3, r3, r3 */
0x100011d4,	/* bl impvt_Sodd */
0x680fe301,	/* rshi r3, r3, r31 >> 1 */
0x100801c2,	/* b impvt_Loop */
/*impvt_Sodd: */
0x500c6700,	/* add r3, r7, r3 */
0x509bff00,	/* addc r6, r31, r31 */
0x680cc301,	/* rshi r3, r3, r6 >> 1 */
0x100801c2,	/* b impvt_Loop */
/*impvt_UVodd: */
0x5c008500,	/* cmp r5, r4 */
0x100881dd,	/* bnc impvt_V>=U */
0xa0086200,	/* subm r2, r2, r3 */
0x5410a400,	/* sub r4, r4, r5 */
0x100801c2,	/* b impvt_Loop */
/*impvt_V>=U: */
0xa00c4300,	/* subm r3, r3, r2 */
0x54148500,	/* sub r5, r5, r4 */
0x100841c2,	/* bnz impvt_Loop */
0x9c07e200,	/* addm r1, r2, r31 */
0x0c000000,	/* ret */
/* } */
/* @0x1e2: function p256verify[85] { */
#define CF_p256verify_adr 482
0x84184000,	/* ldi r6, [#0] */
0x95800600,	/* lddmp r6 */
0x81980018,	/* movi r6.3l, #24 */
0x82180000,	/* movi r6.4l, #0 */
0x82980008,	/* movi r6.5l, #8 */
0x83180009,	/* movi r6.6l, #9 */
0x81180018,	/* movi r6.2l, #24 */
0x97800600,	/* ldrfp r6 */
0x8c0c1300,	/* ld *3, *3 */
0x8c101400,	/* ld *4, *4 */
0x7c600600,	/* mov r24, r6 */
0x48630000,	/* not r24, r24 */
0x84742200,	/* ldci r29, [#16] */
0x84702220,	/* ldci r28, [#17] */
0x98801d00,	/* ldmod r29 */
0x5c03e000,	/* cmp r0, r31 */
0x10004235,	/* bz fail */
0x5c03a000,	/* cmp r0, r29 */
0x10088235,	/* bnc fail */
0x5c03e600,	/* cmp r6, r31 */
0x10004235,	/* bz fail */
0x5c03a600,	/* cmp r6, r29 */
0x10088235,	/* bnc fail */
0x8c0c1300,	/* ld *3, *3 */
0x080001bd,	/* call &ModInvVar */
0x7c640100,	/* mov r25, r1 */
0x08000029,	/* call &MulMod */
0x7c001300,	/* mov r0, r19 */
0x8c081200,	/* ld *2, *2 */
0x7c640100,	/* mov r25, r1 */
0x08000029,	/* call &MulMod */
0x7c041300,	/* mov r1, r19 */
0x847421c0,	/* ldci r29, [#14] */
0x847021e0,	/* ldci r28, [#15] */
0x98801d00,	/* ldmod r29 */
0x8c141500,	/* ld *5, *5 */
0x8c181600,	/* ld *6, *6 */
0x7c281e00,	/* mov r10, r30 */
0x842c2300,	/* ldci r11, [#24] */
0x84302320,	/* ldci r12, [#25] */
0x7c341e00,	/* mov r13, r30 */
0x08000067,	/* call &ProjAdd */
0x7c0c0b00,	/* mov r3, r11 */
0x7c100c00,	/* mov r4, r12 */
0x7c140d00,	/* mov r5, r13 */
0x40082000,	/* and r2, r0, r1 */
0x7c2c1f00,	/* mov r11, r31 */
0x7c301e00,	/* mov r12, r30 */
0x7c341f00,	/* mov r13, r31 */
0x05100019,	/* loop #256 ( */
0x7c200b00,	/* mov r8, r11 */
0x7c240c00,	/* mov r9, r12 */
0x7c280d00,	/* mov r10, r13 */
0x08000067,	/* call &ProjAdd */
0x50084200,	/* add r2, r2, r2 */
0x1008821f,	/* bnc noBoth */
0x7c200300,	/* mov r8, r3 */
0x7c240400,	/* mov r9, r4 */
0x7c280500,	/* mov r10, r5 */
0x08000067,	/* call &ProjAdd */
0x1008022b,	/* b noY */
/*noBoth: */
0x50180000,	/* add r6, r0, r0 */
0x10088225,	/* bnc noG */
0x8c141500,	/* ld *5, *5 */
0x8c181600,	/* ld *6, *6 */
0x7c281e00,	/* mov r10, r30 */
0x08000067,	/* call &ProjAdd */
/*noG: */
0x50182100,	/* add r6, r1, r1 */
0x1008822b,	/* bnc noY */
0x84202300,	/* ldci r8, [#24] */
0x84242320,	/* ldci r9, [#25] */
0x7c281e00,	/* mov r10, r30 */
0x08000067,	/* call &ProjAdd */
/*noY: */
0x50000000,	/* add r0, r0, r0 */
0x50042100,	/* add r1, r1, r1 */
/*		   ) */
0x7c000d00,	/* mov r0, r13 */
0x080001bd,	/* call &ModInvVar */
0x7c600100,	/* mov r24, r1 */
0x7c640b00,	/* mov r25, r11 */
0x08000029,	/* call &MulMod */
0x84742200,	/* ldci r29, [#16] */
0x98801d00,	/* ldmod r29 */
0xa063f300,	/* subm r24, r19, r31 */
/*fail: */
0x90440300,	/* st *3, *1 */
0x0c000000,	/* ret */
/* } */
/* @0x237: function p256scalarmult[12] { */
#define CF_p256scalarmult_adr 567
0x84004000,	/* ldi r0, [#0] */
0x95800000,	/* lddmp r0 */
0x80000000,	/* movi r0.0l, #0 */
0x80800001,	/* movi r0.1l, #1 */
0x81000018,	/* movi r0.2l, #24 */
0x8180000b,	/* movi r0.3l, #11 */
0x97800000,	/* ldrfp r0 */
0x8c001000,	/* ld *0, *0 */
0x0800014c,	/* call &ScalarMult_internal */
0x90540b00,	/* st *3++, *5 */
0x90580b00,	/* st *3++, *6 */
0x0c000000,	/* ret */
/* } */
/* @0x243: function d0inv[14] { */
#define CF_d0inv_adr 579
0x4c000000,	/* xor r0, r0, r0 */
0x80000001,	/* movi r0.0l, #1 */
0x7c740000,	/* mov r29, r0 */
0x05100008,	/* loop #256 ( */
0x5807bc00,	/* mul128 r1, r28l, r29l */
0x588bbc00,	/* mul128 r2, r28u, r29l */
0x50044110,	/* add r1, r1, r2 << 128 */
0x590bbc00,	/* mul128 r2, r28l, r29u */
0x50044110,	/* add r1, r1, r2 << 128 */
0x40040100,	/* and r1, r1, r0 */
0x44743d00,	/* or r29, r29, r1 */
0x50000000,	/* add r0, r0, r0 */
/*		   ) */
0x5477bf00,	/* sub r29, r31, r29 */
0x0c000000,	/* ret */
/* } */
/* @0x251: function selcxSub[10] { */
#define CF_selcxSub_adr 593
0x97800100,	/* ldrfp r1 */
0x95800000,	/* lddmp r0 */
0x540c6300,	/* sub r3, r3, r3 */
0x0600c005,	/* loop *6 ( */
0x8c081800,	/* ld *2, *0++ */
0x7c8c0000,	/* ldr *3, *0 */
0x54906200,	/* subb r4, r2, r3 */
0x66084408,	/* selcx r2, r4, r2 */
0x7ca00300,	/* ldr *0++, *3 */
/*		   ) */
0x0c000000,	/* ret */
/* } */
/* @0x25b: function computeRR[40] { */
#define CF_computeRR_adr 603
0x4c7fff00,	/* xor r31, r31, r31 */
0x84004000,	/* ldi r0, [#0] */
0x95800000,	/* lddmp r0 */
0x840c20c0,	/* ldci r3, [#6] */
0x40040398,	/* and r1, r3, r0 >> 192 */
0x480c6000,	/* not r3, r3 */
0x400c0300,	/* and r3, r3, r0 */
0x500c2301,	/* add r3, r3, r1 << 8 */
0x94800300,	/* ldlc r3 */
0x80040005,	/* movi r1.0l, #5 */
0x81040003,	/* movi r1.2l, #3 */
0x81840002,	/* movi r1.3l, #2 */
0x82040004,	/* movi r1.4l, #4 */
0x97800100,	/* ldrfp r1 */
0x4c0c6300,	/* xor r3, r3, r3 */
0x0600c001,	/* loop *6 ( */
0x7ca00200,	/* ldr *0++, *2 */
/*		   ) */
0x560c1f00,	/* subx r3, r31, r0 */
0x08000251,	/* call &selcxSub */
0x06000010,	/* loop *0 ( */
0x97800100,	/* ldrfp r1 */
0x560c6300,	/* subx r3, r3, r3 */
0x0600c003,	/* loop *6 ( */
0x7c8c0000,	/* ldr *3, *0 */
0x52884200,	/* addcx r2, r2, r2 */
0x7ca00300,	/* ldr *0++, *3 */
/*		   ) */
0x08000251,	/* call &selcxSub */
0x97800100,	/* ldrfp r1 */
0x95800000,	/* lddmp r0 */
0x560c6300,	/* subx r3, r3, r3 */
0x0600c003,	/* loop *6 ( */
0x8c081800,	/* ld *2, *0++ */
0x7c8c0800,	/* ldr *3, *0++ */
0x5e804300,	/* cmpbx r3, r2 */
/*		   ) */
0x08000251,	/* call &selcxSub */
0xfc000000,	/* nop */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x0600c001,	/* loop *6 ( */
0x90680800,	/* st *0++, *2++ */
/*		   ) */
0x0c000000,	/* ret */
/* } */
/* @0x283: function dmXd0[9] { */
#define CF_dmXd0_adr 643
0x586f3e00,	/* mul128 r27, r30l, r25l */
0x59eb3e00,	/* mul128 r26, r30u, r25u */
0x58df3e00,	/* mul128 r23, r30u, r25l */
0x506efb10,	/* add r27, r27, r23 << 128 */
0x50eafa90,	/* addc r26, r26, r23 >> 128 */
0x595f3e00,	/* mul128 r23, r30l, r25u */
0x506efb10,	/* add r27, r27, r23 << 128 */
0x50eafa90,	/* addc r26, r26, r23 >> 128 */
0x0c000000,	/* ret */
/* } */
/* @0x28c: function dmXa[9] { */
#define CF_dmXa_adr 652
0x586c5e00,	/* mul128 r27, r30l, r2l */
0x59e85e00,	/* mul128 r26, r30u, r2u */
0x58dc5e00,	/* mul128 r23, r30u, r2l */
0x506efb10,	/* add r27, r27, r23 << 128 */
0x50eafa90,	/* addc r26, r26, r23 >> 128 */
0x595c5e00,	/* mul128 r23, r30l, r2u */
0x506efb10,	/* add r27, r27, r23 << 128 */
0x50eafa90,	/* addc r26, r26, r23 >> 128 */
0x0c000000,	/* ret */
/* } */
/* @0x295: function mma[46] { */
#define CF_mma_adr 661
0x8204001e,	/* movi r1.4l, #30 */
0x82840018,	/* movi r1.5l, #24 */
0x97800100,	/* ldrfp r1 */
0x8c101b00,	/* ld *4, *3++ */
0x0800028c,	/* call &dmXa */
0x7c940800,	/* ldr *5, *0++ */
0x507b1b00,	/* add r30, r27, r24 */
0x50f7fa00,	/* addc r29, r26, r31 */
0x7c640300,	/* mov r25, r3 */
0x08000283,	/* call &dmXd0 */
0x7c641b00,	/* mov r25, r27 */
0x7c701a00,	/* mov r28, r26 */
0x7c601e00,	/* mov r24, r30 */
0x8c101800,	/* ld *4, *0++ */
0x08000283,	/* call &dmXd0 */
0x506f1b00,	/* add r27, r27, r24 */
0x50f3fa00,	/* addc r28, r26, r31 */
0x0600e00e,	/* loop *7 ( */
0x8c101b00,	/* ld *4, *3++ */
0x0800028c,	/* call &dmXa */
0x7c940800,	/* ldr *5, *0++ */
0x506f1b00,	/* add r27, r27, r24 */
0x50ebfa00,	/* addc r26, r26, r31 */
0x5063bb00,	/* add r24, r27, r29 */
0x50f7fa00,	/* addc r29, r26, r31 */
0x8c101800,	/* ld *4, *0++ */
0x08000283,	/* call &dmXd0 */
0x506f1b00,	/* add r27, r27, r24 */
0x50ebfa00,	/* addc r26, r26, r31 */
0x52639b00,	/* addx r24, r27, r28 */
0x7ca80500,	/* ldr *2++, *5 */
0x52f3fa00,	/* addcx r28, r26, r31 */
/*		   ) */
0x52e39d00,	/* addcx r24, r29, r28 */
0x7ca80500,	/* ldr *2++, *5 */
0x95800000,	/* lddmp r0 */
0x97800100,	/* ldrfp r1 */
0x54739c00,	/* sub r28, r28, r28 */
0x0600c007,	/* loop *6 ( */
0x8c141800,	/* ld *5, *0++ */
0x7c900000,	/* ldr *4, *0 */
0x54f71e00,	/* subb r29, r30, r24 */
0x99600000,	/* strnd r24 */
0x7c800500,	/* ldr *0, *5 */
0x6663dd08,	/* selcx r24, r29, r30 */
0x7ca00500,	/* ldr *0++, *5 */
/*		   ) */
0x0c000000,	/* ret */
/* } */
/* @0x2c3: function setupPtrs[10] { */
#define CF_setupPtrs_adr 707
0x4c7fff00,	/* xor r31, r31, r31 */
0x95800000,	/* lddmp r0 */
0x94800000,	/* ldlc r0 */
0x4c042100,	/* xor r1, r1, r1 */
0x80040004,	/* movi r1.0l, #4 */
0x80840003,	/* movi r1.1l, #3 */
0x81040004,	/* movi r1.2l, #4 */
0x81840002,	/* movi r1.3l, #2 */
0x97800100,	/* ldrfp r1 */
0x0c000000,	/* ret */
/* } */
/* @0x2cd: function mulx[19] { */
#define CF_mulx_adr 717
0x84004000,	/* ldi r0, [#0] */
0x080002c3,	/* call &setupPtrs */
0x8c041100,	/* ld *1, *1 */
0x4c084200,	/* xor r2, r2, r2 */
0x0600c001,	/* loop *6 ( */
0x7ca80300,	/* ldr *2++, *3 */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x0600c004,	/* loop *6 ( */
0x8c0c1c00,	/* ld *3, *4++ */
0x95000000,	/* stdmp r0 */
0x08000295,	/* call &mma */
0x95800000,	/* lddmp r0 */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x95800000,	/* lddmp r0 */
0x0600c001,	/* loop *6 ( */
0x90740800,	/* st *0++, *5++ */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x95800000,	/* lddmp r0 */
0x0c000000,	/* ret */
/* } */
/* @0x2e0: function mul1_exp[30] { */
#define CF_mul1_exp_adr 736
0x8c041100,	/* ld *1, *1 */
0x4c084200,	/* xor r2, r2, r2 */
0x0600c001,	/* loop *6 ( */
0x7ca80300,	/* ldr *2++, *3 */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x80080001,	/* movi r2.0l, #1 */
0x0600c003,	/* loop *6 ( */
0x95800000,	/* lddmp r0 */
0x08000295,	/* call &mma */
0x4c084200,	/* xor r2, r2, r2 */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x95800000,	/* lddmp r0 */
0x56084200,	/* subx r2, r2, r2 */
0x0600c003,	/* loop *6 ( */
0x8c041800,	/* ld *1, *0++ */
0x7c8c0800,	/* ldr *3, *0++ */
0x5e804300,	/* cmpbx r3, r2 */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x95800000,	/* lddmp r0 */
0x540c6300,	/* sub r3, r3, r3 */
0x0600c006,	/* loop *6 ( */
0x8c041800,	/* ld *1, *0++ */
0x7c8c0800,	/* ldr *3, *0++ */
0x548c6200,	/* subb r3, r2, r3 */
0x66084308,	/* selcx r2, r3, r2 */
0x90740300,	/* st *3, *5++ */
0xfc000000,	/* nop */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x95800000,	/* lddmp r0 */
0x0c000000,	/* ret */
/* } */
/* @0x2fe: function mul1[4] { */
#define CF_mul1_adr 766
0x84004000,	/* ldi r0, [#0] */
0x080002c3,	/* call &setupPtrs */
0x080002e0,	/* call &mul1_exp */
0x0c000000,	/* ret */
/* } */
/* @0x302: function sqrx_exp[19] { */
#define CF_sqrx_exp_adr 770
0x84004020,	/* ldi r0, [#1] */
0x95800000,	/* lddmp r0 */
0x8c041100,	/* ld *1, *1 */
0x4c084200,	/* xor r2, r2, r2 */
0x0600c001,	/* loop *6 ( */
0x7ca80300,	/* ldr *2++, *3 */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x0600c004,	/* loop *6 ( */
0x8c0c1c00,	/* ld *3, *4++ */
0x95000000,	/* stdmp r0 */
0x08000295,	/* call &mma */
0x95800000,	/* lddmp r0 */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x95800000,	/* lddmp r0 */
0x0600c001,	/* loop *6 ( */
0x90740800,	/* st *0++, *5++ */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x95800000,	/* lddmp r0 */
0x0c000000,	/* ret */
/* } */
/* @0x315: function mulx_exp[14] { */
#define CF_mulx_exp_adr 789
0x84004040,	/* ldi r0, [#2] */
0x95800000,	/* lddmp r0 */
0x8c041100,	/* ld *1, *1 */
0x4c084200,	/* xor r2, r2, r2 */
0x0600c001,	/* loop *6 ( */
0x7ca80300,	/* ldr *2++, *3 */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x0600c004,	/* loop *6 ( */
0x8c0c1c00,	/* ld *3, *4++ */
0x95000000,	/* stdmp r0 */
0x08000295,	/* call &mma */
0x95800000,	/* lddmp r0 */
/*		   ) */
0x97800100,	/* ldrfp r1 */
0x0c000000,	/* ret */
/* } */
/* @0x323: function modexp[42] { */
#define CF_modexp_adr 803
0x080002cd,	/* call &mulx */
0x84004060,	/* ldi r0, [#3] */
0x95800000,	/* lddmp r0 */
0x54084200,	/* sub r2, r2, r2 */
0x0600c004,	/* loop *6 ( */
0xfc000000,	/* nop */
0x8c0c1800,	/* ld *3, *0++ */
0x54885f00,	/* subb r2, r31, r2 */
0x90740300,	/* st *3, *5++ */
/*		   ) */
0xfc000000,	/* nop */
0x840820c0,	/* ldci r2, [#6] */
0x400c0298,	/* and r3, r2, r0 >> 192 */
0x48084000,	/* not r2, r2 */
0x40080200,	/* and r2, r2, r0 */
0x50086201,	/* add r2, r2, r3 << 8 */
0x94800200,	/* ldlc r2 */
0x06000015,	/* loop *0 ( */
0x08000302,	/* call &sqrx_exp */
0x08000315,	/* call &mulx_exp */
0x84004060,	/* ldi r0, [#3] */
0x95800000,	/* lddmp r0 */
0x99080000,	/* strnd r2 */
0x54084200,	/* sub r2, r2, r2 */
0x0600c004,	/* loop *6 ( */
0x99080000,	/* strnd r2 */
0x8c0c1400,	/* ld *3, *4 */
0x50884200,	/* addc r2, r2, r2 */
0x90700300,	/* st *3, *4++ */
/*		   ) */
0x0600c008,	/* loop *6 ( */
0x99080000,	/* strnd r2 */
0x8c041500,	/* ld *1, *5 */
0x90540300,	/* st *3, *5 */
0x7c8c0800,	/* ldr *3, *0++ */
0x7c000200,	/* mov r0, r2 */
0x99080000,	/* strnd r2 */
0x64086008,	/* selc r2, r0, r3 */
0x90740300,	/* st *3, *5++ */
/*		   ) */
0xfc000000,	/* nop */
/*		   ) */
0x84004060,	/* ldi r0, [#3] */
0x95800000,	/* lddmp r0 */
0x080002e0,	/* call &mul1_exp */
0x0c000000,	/* ret */
/* } */
/* @0x34d: function modload[12] { */
#define CF_modload_adr 845
0x4c7fff00,	/* xor r31, r31, r31 */
0x84004000,	/* ldi r0, [#0] */
0x95800000,	/* lddmp r0 */
0x94800000,	/* ldlc r0 */
0x8000001c,	/* movi r0.0l, #28 */
0x8080001d,	/* movi r0.1l, #29 */
0x97800000,	/* ldrfp r0 */
0x8c001000,	/* ld *0, *0 */
0x08000243,	/* call &d0inv */
0x90440100,	/* st *1, *1 */
0x0800025b,	/* call &computeRR */
0x0c000000,	/* ret */
/* } */
};

struct DMEM_montmul_ptrs {
	uint32_t pMod;
	uint32_t pDinv;
	uint32_t pRR;
	uint32_t pA;
	uint32_t pB;
	uint32_t pC;
	uint32_t n;
	uint32_t n1;
};

/*
 * This struct is "calling convention" for passing parameters into the
 * code block above for RSA operations.  Parameters start at &DMEM[0].
 */
struct DMEM_montmul {
	struct DMEM_montmul_ptrs in_ptrs;
	struct DMEM_montmul_ptrs sqr_ptrs;
	struct DMEM_montmul_ptrs mul_ptrs;
	struct DMEM_montmul_ptrs out_ptrs;
	uint32_t mod[RSA_WORDS_4K];
	uint32_t dInv[8];
	uint32_t RR[RSA_WORDS_4K];
	uint32_t in[RSA_WORDS_4K];
	uint32_t exp[RSA_WORDS_4K];
	uint32_t out[RSA_WORDS_4K];
};

#define DMEM_CELL_SIZE 32
#define DMEM_INDEX(p, f) \
	(((const uint8_t *)&(p)->f - (const uint8_t *)(p)) / DMEM_CELL_SIZE)

/* output = input ** exp % N. */
/* TODO(ngm): add blinding; measure timing. */
/* TODO(ngm): propagate return code. */
void bn_mont_modexp_asm(struct LITE_BIGNUM *output,
		const struct LITE_BIGNUM *input,
		const struct LITE_BIGNUM *exp,
		const struct LITE_BIGNUM *N) {
	int i, result;
	struct DMEM_montmul *montmul;

	/* Initialize hardware; load code page. */
	dcrypto_init();
	dcrypto_imem_load(0, IMEM_dcrypto, ARRAY_SIZE(IMEM_dcrypto));

	/* Point to DMEM. */
	montmul = (struct DMEM_montmul *)GREG32_ADDR(CRYPTO, DMEM_DUMMY);

	/* Setup DMEM pointers (as indices into DMEM which 256-bit cells). */
	montmul->in_ptrs.pMod = DMEM_INDEX(montmul, mod);
	montmul->in_ptrs.pDinv = DMEM_INDEX(montmul, dInv);
	montmul->in_ptrs.pRR = DMEM_INDEX(montmul, RR);
	montmul->in_ptrs.pA = DMEM_INDEX(montmul, in);
	montmul->in_ptrs.pB = DMEM_INDEX(montmul, exp);
	montmul->in_ptrs.pC = DMEM_INDEX(montmul, out);
	montmul->in_ptrs.n = bn_bits(N) / (DMEM_CELL_SIZE * 8);
	montmul->in_ptrs.n1 = montmul->in_ptrs.n - 1;

	montmul->sqr_ptrs = montmul->in_ptrs;
	montmul->mul_ptrs = montmul->in_ptrs;
	montmul->out_ptrs = montmul->in_ptrs;

	dcrypto_dmem_load(DMEM_INDEX(montmul, mod), N->d, bn_words(N));
	dcrypto_dmem_load(DMEM_INDEX(montmul, in), input->d, bn_words(input));
	dcrypto_dmem_load(DMEM_INDEX(montmul, exp), exp->d, bn_words(exp));

	/* 0 pad the exponent to full size. */
	for (i = bn_words(exp); i < bn_words(N); ++i)
		montmul->exp[i] = 0;

	/* Calculate RR and d0inv. */
	result = dcrypto_call(CF_modload_adr);

	if (bn_words(exp) > 1) {
		/* in = in * RR */
		montmul->in_ptrs.pA = DMEM_INDEX(montmul, in);
		montmul->in_ptrs.pB = DMEM_INDEX(montmul, RR);
		montmul->in_ptrs.pC = DMEM_INDEX(montmul, in);

		/* out = out * out */
		montmul->sqr_ptrs.pA = DMEM_INDEX(montmul, out);
		montmul->sqr_ptrs.pB = DMEM_INDEX(montmul, out);
		montmul->sqr_ptrs.pC = DMEM_INDEX(montmul, out);

		/* out = out * in */
		montmul->mul_ptrs.pA = DMEM_INDEX(montmul, in);
		montmul->mul_ptrs.pB = DMEM_INDEX(montmul, out);
		montmul->mul_ptrs.pC = DMEM_INDEX(montmul, out);

		/* out = out / R */
		montmul->out_ptrs.pA = DMEM_INDEX(montmul, out);
		montmul->out_ptrs.pB = DMEM_INDEX(montmul, exp);
		montmul->out_ptrs.pC = DMEM_INDEX(montmul, out);

		result |= dcrypto_call(CF_modexp_adr);
	} else {
		/* Small public exponent */
		uint32_t e = BN_DIGIT(exp, 0);
		uint32_t b = 0x80000000;

		while (b != 0 && !(b & e))
			b >>= 1;

		/* out = in * RR */
		montmul->in_ptrs.pA = DMEM_INDEX(montmul, in);
		montmul->in_ptrs.pB = DMEM_INDEX(montmul, RR);
		montmul->in_ptrs.pC = DMEM_INDEX(montmul, out);
		result |= dcrypto_call(CF_mulx_adr);

		/* in = in * RR */
		montmul->in_ptrs.pC = DMEM_INDEX(montmul, in);
		result |= dcrypto_call(CF_mulx_adr);

		b >>= 1;

		while (b != 0) {
			/* out = out * out */
			montmul->in_ptrs.pA = DMEM_INDEX(montmul, out);
			montmul->in_ptrs.pB = DMEM_INDEX(montmul, out);
			montmul->in_ptrs.pC = DMEM_INDEX(montmul, out);
			result |= dcrypto_call(CF_mulx_adr);

			if ((b & e) != 0) {
				/* out = out * in */
				montmul->in_ptrs.pA = DMEM_INDEX(montmul, in);
				montmul->in_ptrs.pB = DMEM_INDEX(montmul, out);
				montmul->in_ptrs.pC = DMEM_INDEX(montmul, out);
				result |= dcrypto_call(CF_mulx_adr);
			}

			b >>= 1;
		}
		/* out = out / R */
		montmul->in_ptrs.pA = DMEM_INDEX(montmul, out);
		montmul->in_ptrs.pB = DMEM_INDEX(montmul, out);
		montmul->in_ptrs.pC = DMEM_INDEX(montmul, out);
		result |= dcrypto_call(CF_mul1_adr);
	}

	memcpy(output->d, montmul->out, bn_size(output));

	(void) (result == 0); /* end of errorcode propagation */
}

/*
 * This struct is "calling convention" for passing parameters into the
 * code block above for ecc operations.  Parameters start at &DMEM[0].
 */
struct DMEM_ecc {
	uint32_t pK;
	uint32_t pRnd;
	uint32_t pMsg;
	uint32_t pR;
	uint32_t pS;
	uint32_t pX;
	uint32_t pY;
	uint32_t pD;
	p256_int k;
	p256_int rnd;
	p256_int msg;
	p256_int r;
	p256_int s;
	p256_int x;
	p256_int y;
	p256_int d;
};

static void dcrypto_ecc_init(void)
{
	struct DMEM_ecc *pEcc =
		(struct DMEM_ecc *)GREG32_ADDR(CRYPTO, DMEM_DUMMY);

	dcrypto_init();
	dcrypto_imem_load(0, IMEM_dcrypto, ARRAY_SIZE(IMEM_dcrypto));

	pEcc->pK = DMEM_INDEX(pEcc, k);
	pEcc->pRnd = DMEM_INDEX(pEcc, rnd);
	pEcc->pMsg = DMEM_INDEX(pEcc, msg);
	pEcc->pR = DMEM_INDEX(pEcc, r);
	pEcc->pS = DMEM_INDEX(pEcc, s);
	pEcc->pX = DMEM_INDEX(pEcc, x);
	pEcc->pY = DMEM_INDEX(pEcc, y);
	pEcc->pD = DMEM_INDEX(pEcc, d);

	/* (over)write first words to ensure pairwise mismatch. */
	pEcc->k.a[0] = 1;
	pEcc->rnd.a[0] = 2;
	pEcc->msg.a[0] = 3;
	pEcc->r.a[0] = 4;
	pEcc->s.a[0] = 5;
	pEcc->x.a[0] = 6;
	pEcc->y.a[0] = 7;
	pEcc->d.a[0] = 8;
}

/*
 * Local copy function since for some reason we have p256_int as
 * packed structs.
 * This causes wrong writes (bytes vs. words) to the peripheral with
 * struct copies in case the src operand is unaligned.
 *
 * Our peripheral dst are always aligned correctly.
 * By making sure the src is aligned too, we get word copy behavior.
 */
static inline void cp8w(p256_int *dst, const p256_int *src)
{
	p256_int tmp;

	tmp = *src;
	*dst = tmp;
}

int dcrypto_p256_ecdsa_sign(const p256_int *key, const p256_int *message,
		p256_int *r, p256_int *s)
{
	int i, result;
	struct DMEM_ecc *pEcc =
		(struct DMEM_ecc *)GREG32_ADDR(CRYPTO, DMEM_DUMMY);

	dcrypto_ecc_init();
	result = dcrypto_call(CF_p256init_adr);

	/* Pick uniform 0 < k < R */
	do {
		for (i = 0; i < 8; ++i)
			pEcc->rnd.a[i] ^= rand();
	} while (p256_cmp(&SECP256r1_nMin2, &pEcc->rnd) < 0);

	p256_add_d(&pEcc->rnd, 1, &pEcc->k);

	for (i = 0; i < 8; ++i)
		pEcc->rnd.a[i] = rand();

	cp8w(&pEcc->msg, message);
	cp8w(&pEcc->d, key);

	result |= dcrypto_call(CF_p256sign_adr);

	cp8w(r, &pEcc->r);
	cp8w(s, &pEcc->s);

	/* Wipe d,k */
	cp8w(&pEcc->d, &pEcc->rnd);
	cp8w(&pEcc->k, &pEcc->rnd);

	return result == 0;
}

int dcrypto_p256_base_point_mul(const p256_int *k, p256_int *x, p256_int *y)
{
	int i, result;
	struct DMEM_ecc *pEcc =
		(struct DMEM_ecc *)GREG32_ADDR(CRYPTO, DMEM_DUMMY);

	dcrypto_ecc_init();
	result = dcrypto_call(CF_p256init_adr);

	for (i = 0; i < 8; ++i)
		pEcc->rnd.a[i] ^= rand();

	cp8w(&pEcc->d, k);

	result |= dcrypto_call(CF_p256scalarbasemult_adr);

	cp8w(x, &pEcc->x);
	cp8w(y, &pEcc->y);

	/* Wipe d */
	cp8w(&pEcc->d, &pEcc->rnd);

	return result == 0;
}

int dcrypto_p256_point_mul(const p256_int *k,
		const p256_int *in_x, const p256_int *in_y,
		p256_int *x, p256_int *y)
{
	int i, result;
	struct DMEM_ecc *pEcc =
		(struct DMEM_ecc *)GREG32_ADDR(CRYPTO, DMEM_DUMMY);

	dcrypto_ecc_init();
	result = dcrypto_call(CF_p256init_adr);

	for (i = 0; i < 8; ++i)
		pEcc->rnd.a[i] ^= rand();

	cp8w(&pEcc->k, k);
	cp8w(&pEcc->x, in_x);
	cp8w(&pEcc->y, in_y);

	result |= dcrypto_call(CF_p256scalarmult_adr);

	cp8w(x, &pEcc->x);
	cp8w(y, &pEcc->y);

	/* Wipe k,x,y */
	cp8w(&pEcc->k, &pEcc->rnd);
	cp8w(&pEcc->x, &pEcc->rnd);
	cp8w(&pEcc->y, &pEcc->rnd);

	return result == 0;
}

int dcrypto_p256_ecdsa_verify(const p256_int *key_x, const p256_int *key_y,
		const p256_int *message, const p256_int *r,
		const p256_int *s)
{
	int i, result;
	struct DMEM_ecc *pEcc =
		(struct DMEM_ecc *)GREG32_ADDR(CRYPTO, DMEM_DUMMY);

	dcrypto_ecc_init();
	result = dcrypto_call(CF_p256init_adr);

	cp8w(&pEcc->msg, message);
	cp8w(&pEcc->r, r);
	cp8w(&pEcc->s, s);
	cp8w(&pEcc->x, key_x);
	cp8w(&pEcc->y, key_y);

	result |= dcrypto_call(CF_p256verify_adr);

	for (i = 0; i < 8; ++i)
		result |= (pEcc->rnd.a[i] ^ r->a[i]);

	return result == 0;
}

int dcrypto_p256_is_valid_point(const p256_int *x, const p256_int *y)
{
	int i, result;
	struct DMEM_ecc *pEcc =
		(struct DMEM_ecc *)GREG32_ADDR(CRYPTO, DMEM_DUMMY);

	dcrypto_ecc_init();
	result = dcrypto_call(CF_p256init_adr);

	cp8w(&pEcc->x, x);
	cp8w(&pEcc->y, y);

	result |= dcrypto_call(CF_p256isoncurve_adr);

	for (i = 0; i < 8; ++i)
		result |= (pEcc->r.a[i] ^ pEcc->s.a[i]);

	return result == 0;
}
