/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "internal.h"
#include "registers.h"
#include "trng.h"

/* Firmware blob for crypto accelerator */

/* AUTO-GENERATED.  DO NOT MODIFY. */
const uint32_t IMEM_dcrypto[] = {

#define CF_vectors_adr 0
	0x10080010,
	0x1008000f,
	0x1008004f,
	0x1008022d,
	0x100801a8,
	0x10080181,
	0x100801e2,
	0x1008000f,
	0x1008000f,
	0x1008000f,
	0x10080319,
	0x100802f4,
	0x100802c3,
	0x10080343,
	0x0c000000,


#define CF___notused_adr 15
	0x0c000000,


#define CF_p256init_adr 16
	0xfc000000,
	0x4c7fff00,
	0x4c7bde00,
	0x80780001,
	0x847421c0,
	0x847021e0,
	0x98801d00,
	0x7c6c1f00,
	0x83ed5ac6,
	0x83ec35d8,
	0x836daa3a,
	0x836c93e7,
	0x82edb3eb,
	0x82ecbd55,
	0x826d7698,
	0x826c86bc,
	0x81ed651d,
	0x81ec06b0,
	0x816dcc53,
	0x816cb0f6,
	0x80ed3bce,
	0x80ec3c3e,
	0x806d27d2,
	0x806c604b,
	0x0c000000,


#define CF_MulMod_adr 41
	0x584f3800,
	0x59d33800,
	0x58d73800,
	0x504eb310,
	0x50d2b490,
	0x59573800,
	0x504eb310,
	0x50d2b490,
	0x645bfc02,
	0x685693ff,
	0x585f9500,
	0x59e39500,
	0x58e79500,
	0x505f3710,
	0x50e33890,
	0x59679500,
	0x505f3710,
	0x50e33890,
	0x6867f4ff,
	0x5062b800,
	0x50e7f900,
	0x5062d800,
	0x50e7f900,
	0x68573801,
	0x585abd00,
	0x59debd00,
	0x58e2bd00,
	0x505b1610,
	0x50df1790,
	0x5962bd00,
	0x505b1610,
	0x50df1790,
	0x544ed300,
	0x54d2f400,
	0x6457fd01,
	0x544eb300,
	0x9c4ff300,
	0x0c000000,


#define CF_p256isoncurve_adr 79
	0x84004000,
	0x95800000,
	0x82800018,
	0x83000018,
	0x80000000,
	0x97800000,
	0x8c181600,
	0x7c641800,
	0x08000029,
	0x7c001300,
	0x8c141500,
	0x7c641800,
	0x08000029,
	0x8c141500,
	0x7c641300,
	0x08000029,
	0x8c141500,
	0xa04f1300,
	0xa04f1300,
	0xa04f1300,
	0x9c637300,
	0x904c0500,
	0x90500000,
	0x0c000000,


#define CF_ProjAdd_adr 103
	0x7c600b00,
	0x7c640800,
	0x08000029,
	0x7c381300,
	0x7c600c00,
	0x7c640900,
	0x08000029,
	0x7c3c1300,
	0x7c600d00,
	0x7c640a00,
	0x08000029,
	0x7c401300,
	0x9c458b00,
	0x9c492800,
	0x7c601100,
	0x7c641200,
	0x08000029,
	0x9c49ee00,
	0xa0465300,
	0x9c49ac00,
	0x9c4d4900,
	0x7c601200,
	0x7c641300,
	0x08000029,
	0x7c481300,
	0x9c4e0f00,
	0xa04a7200,
	0x9c4dab00,
	0x9c314800,
	0x7c601300,
	0x7c640c00,
	0x08000029,
	0x7c2c1300,
	0x9c320e00,
	0xa0318b00,
	0x7c601b00,
	0x7c641000,
	0x08000029,
	0xa02e6c00,
	0x9c356b00,
	0x9c2dab00,
	0xa0356f00,
	0x9c2d6f00,
	0x7c601b00,
	0x7c640c00,
	0x08000029,
	0x9c3e1000,
	0x9c420f00,
	0xa0321300,
	0xa031cc00,
	0x9c3d8c00,
	0x9c318f00,
	0x9c3dce00,
	0x9c39cf00,
	0xa03a0e00,
	0x7c601200,
	0x7c640c00,
	0x08000029,
	0x7c3c1300,
	0x7c600e00,
	0x7c640c00,
	0x08000029,
	0x7c401300,
	0x7c600b00,
	0x7c640d00,
	0x08000029,
	0x9c321300,
	0x7c601100,
	0x7c640b00,
	0x08000029,
	0xa02df300,
	0x7c601200,
	0x7c640d00,
	0x08000029,
	0x7c341300,
	0x7c601100,
	0x7c640e00,
	0x08000029,
	0x9c366d00,
	0x0c000000,


#define CF_ProjToAffine_adr 183
	0x9c2bea00,
	0x7c600a00,
	0x7c640a00,
	0x08000029,
	0x7c601300,
	0x7c640a00,
	0x08000029,
	0x7c301300,
	0x7c601300,
	0x7c641300,
	0x08000029,
	0x7c601300,
	0x7c641300,
	0x08000029,
	0x7c601300,
	0x7c640c00,
	0x08000029,
	0x7c341300,
	0x05004004,
	0x7c601300,
	0x7c641300,
	0x08000029,
	0xfc000000,

	0x7c601300,
	0x7c640d00,
	0x08000029,
	0x7c381300,
	0x05008004,
	0x7c601300,
	0x7c641300,
	0x08000029,
	0xfc000000,

	0x7c601300,
	0x7c640e00,
	0x08000029,
	0x7c3c1300,
	0x05010004,
	0x7c601300,
	0x7c641300,
	0x08000029,
	0xfc000000,

	0x7c601300,
	0x7c640f00,
	0x08000029,
	0x7c401300,
	0x05020004,
	0x7c601300,
	0x7c641300,
	0x08000029,
	0xfc000000,

	0x7c441300,
	0x7c600a00,
	0x7c641300,
	0x08000029,
	0x050c0004,
	0x7c601300,
	0x7c641300,
	0x08000029,
	0xfc000000,

	0x7c481300,
	0x7c601100,
	0x7c641000,
	0x08000029,
	0x05010004,
	0x7c601300,
	0x7c641300,
	0x08000029,
	0xfc000000,

	0x7c600f00,
	0x7c641300,
	0x08000029,
	0x05008004,
	0x7c601300,
	0x7c641300,
	0x08000029,
	0xfc000000,

	0x7c600e00,
	0x7c641300,
	0x08000029,
	0x05004004,
	0x7c601300,
	0x7c641300,
	0x08000029,
	0xfc000000,

	0x7c600d00,
	0x7c641300,
	0x08000029,
	0x05002004,
	0x7c601300,
	0x7c641300,
	0x08000029,
	0xfc000000,

	0x7c600c00,
	0x7c641300,
	0x08000029,
	0x05002004,
	0x7c601300,
	0x7c641300,
	0x08000029,
	0xfc000000,

	0x7c600a00,
	0x7c641300,
	0x08000029,
	0x7c601300,
	0x7c641200,
	0x08000029,
	0x7c381300,
	0x7c600800,
	0x7c640e00,
	0x08000029,
	0x7c2c1300,
	0x7c600900,
	0x7c640e00,
	0x08000029,
	0x7c301300,
	0x0c000000,


#define CF_ModInv_adr 299
	0x98080000,
	0x55080202,
	0x7c041e00,
	0x0510000c,
	0x7c600100,
	0x7c640100,
	0x08000029,
	0x7c0c1300,
	0x50084200,
	0x64046108,
	0x1008813a,
	0x7c600300,
	0x7c640000,
	0x08000029,
	0x7c041300,

	0xfc000000,

	0x0c000000,


#define CF_FetchBandRandomize_adr 316
	0x99080000,
	0x9c6be200,
	0x8c081500,
	0x7c641a00,
	0x08000029,
	0x7c181300,
	0x8c081600,
	0x7c641a00,
	0x08000029,
	0x7c1c1300,
	0x0c000000,


#define CF_ProjDouble_adr 327
	0x7c2c0800,
	0x7c300900,
	0x7c340a00,
	0x08000067,
	0x0c000000,


#define CF_ScalarMult_internal_adr 332
	0x84742200,
	0x98801d00,
	0x8c041100,
	0x9c07e100,
	0xa0002000,
	0x847421c0,
	0x98801d00,
	0x0800013c,
	0x7c200600,
	0x7c240700,
	0x7c281a00,
	0x08000147,
	0x7c0c0b00,
	0x7c100c00,
	0x7c140d00,
	0x7c201f00,
	0x7c241e00,
	0x7c281f00,
	0x05100020,
	0x08000147,
	0x0800013c,
	0x4c202000,
	0x64206602,
	0x64248702,
	0x6428ba02,
	0x7c080b00,
	0x7c180c00,
	0x7c1c0d00,
	0x08000067,
	0x44202000,
	0x64204b02,
	0x6424cc02,
	0x6428ed02,
	0x680000ff,
	0x680421ff,
	0x992c0000,
	0x99300000,
	0x99340000,
	0x99080000,
	0x7c600300,
	0x7c640200,
	0x08000029,
	0x7c0c1300,
	0x7c600400,
	0x7c640200,
	0x08000029,
	0x7c101300,
	0x7c600500,
	0x7c640200,
	0x08000029,
	0x7c141300,

	0x080000b7,
	0x0c000000,


#define CF_p256sign_adr 385
	0xfc000000,
	0x84004000,
	0x95800000,
	0x80000000,
	0x80800001,
	0x81000018,
	0x97800000,
	0x84002300,
	0x90540000,
	0xfc000000,
	0x84002320,
	0x90580000,
	0xfc000000,
	0x8c001000,
	0x0800014c,
	0x84742200,
	0x84702220,
	0x98801d00,
	0x8c001000,
	0x0800012b,
	0x8c081700,
	0x7c640100,
	0x08000029,
	0x9c63eb00,
	0x904c0200,
	0xfc000000,
	0x7c641300,
	0x08000029,
	0x7c001300,
	0x8c081200,
	0x7c640100,
	0x08000029,
	0x9c001300,
	0x90500000,
	0xfc000000,
	0x847421c0,
	0x847021e0,
	0x98801d00,
	0x0c000000,


#define CF_p256scalarbasemult_adr 424
	0xfc000000,
	0x84004000,
	0x95800000,
	0x80000000,
	0x80800001,
	0x81000018,
	0x8180000b,
	0x97800000,
	0x8c001100,
	0x99800000,
	0x84002300,
	0x90540000,
	0xfc000000,
	0x84002320,
	0x90580000,
	0xfc000000,
	0x8c001700,
	0x0800014c,
	0x90540b00,
	0x90580b00,
	0x0c000000,


#define CF_ModInvVar_adr 445
	0x7c081f00,
	0x7c0c1e00,
	0x98100000,
	0x981c0000,
	0x7c140000,

	0x44108400,
	0x100011cd,
	0x6813e401,
	0x44084200,
	0x100011c9,
	0x680be201,
	0x100801c2,

	0x50084700,
	0x509bff00,
	0x6808c201,
	0x100801c2,

	0x4414a500,
	0x100011d8,
	0x6817e501,
	0x440c6300,
	0x100011d4,
	0x680fe301,
	0x100801c2,

	0x500c6700,
	0x509bff00,
	0x680cc301,
	0x100801c2,

	0x5c008500,
	0x100881dd,
	0xa0086200,
	0x5410a400,
	0x100801c2,

	0xa00c4300,
	0x54148500,
	0x100841c2,
	0x9c07e200,
	0x0c000000,


#define CF_p256verify_adr 482
	0xfc000000,
	0x84184000,
	0x95800600,
	0x81980018,
	0x82180000,
	0x82980008,
	0x83180009,
	0x81180018,
	0x97800600,
	0x84742200,
	0x84702220,
	0x98801d00,
	0x8c101400,
	0x080001bd,
	0x8c0c1300,
	0x7c640100,
	0x08000029,
	0x7c001300,
	0x8c081200,
	0x7c640100,
	0x08000029,
	0x7c041300,
	0x847421c0,
	0x847021e0,
	0x98801d00,
	0x8c141500,
	0x8c181600,
	0x7c281e00,
	0x842c2300,
	0x84302320,
	0x7c341e00,
	0x08000067,
	0x7c0c0b00,
	0x7c100c00,
	0x7c140d00,
	0x40082000,
	0x7c2c1f00,
	0x7c301e00,
	0x7c341f00,
	0x05100019,
	0x7c200b00,
	0x7c240c00,
	0x7c280d00,
	0x08000067,
	0x50084200,
	0x10088215,
	0x7c200300,
	0x7c240400,
	0x7c280500,
	0x08000067,
	0x10080221,

	0x50180000,
	0x1008821b,
	0x8c141500,
	0x8c181600,
	0x7c281e00,
	0x08000067,

	0x50182100,
	0x10088221,
	0x84202300,
	0x84242320,
	0x7c281e00,
	0x08000067,

	0x50000000,
	0x50042100,

	0x7c000d00,
	0x080001bd,
	0x7c600100,
	0x7c640b00,
	0x08000029,
	0x84742200,
	0x98801d00,
	0xa063f300,
	0x90440300,
	0x0c000000,


#define CF_p256scalarmult_adr 557
	0x84004000,
	0x95800000,
	0x80000000,
	0x80800001,
	0x81000018,
	0x8180000b,
	0x97800000,
	0x8c001000,
	0x0800014c,
	0x90540b00,
	0x90580b00,
	0x0c000000,


#define CF_d0inv_adr 569
	0x4c000000,
	0x80000001,
	0x7c740000,
	0x05100008,
	0x5807bc00,
	0x588bbc00,
	0x50044110,
	0x590bbc00,
	0x50044110,
	0x40040100,
	0x44743d00,
	0x50000000,

	0x5477bf00,
	0x0c000000,


#define CF_selcxSub_adr 583
	0x97800100,
	0x95800000,
	0x540c6300,
	0x0600c005,
	0x8c081800,
	0x7c8c0000,
	0x54906200,
	0x66084408,
	0x7ca00300,

	0x0c000000,


#define CF_computeRR_adr 593
	0x4c7fff00,
	0x84004000,
	0x95800000,
	0x840c20c0,
	0x40040398,
	0x480c6000,
	0x400c0300,
	0x500c2301,
	0x94800300,
	0x80040005,
	0x81040003,
	0x81840002,
	0x82040004,
	0x97800100,
	0x4c0c6300,
	0x0600c001,
	0x7ca00200,

	0x560c1f00,
	0x08000247,
	0x06000010,
	0x97800100,
	0x560c6300,
	0x0600c003,
	0x7c8c0000,
	0x52884200,
	0x7ca00300,

	0x08000247,
	0x97800100,
	0x95800000,
	0x560c6300,
	0x0600c003,
	0x8c081800,
	0x7c8c0800,
	0x5e804300,

	0x08000247,
	0xfc000000,

	0x97800100,
	0x0600c001,
	0x90680800,

	0x0c000000,


#define CF_dmXd0_adr 633
	0x586f3e00,
	0x59eb3e00,
	0x58df3e00,
	0x506efb10,
	0x50eafa90,
	0x595f3e00,
	0x506efb10,
	0x50eafa90,
	0x0c000000,


#define CF_dmXa_adr 642
	0x586c5e00,
	0x59e85e00,
	0x58dc5e00,
	0x506efb10,
	0x50eafa90,
	0x595c5e00,
	0x506efb10,
	0x50eafa90,
	0x0c000000,


#define CF_mma_adr 651
	0x8204001e,
	0x82840018,
	0x97800100,
	0x8c101b00,
	0x08000282,
	0x7c940800,
	0x507b1b00,
	0x50f7fa00,
	0x7c640300,
	0x08000279,
	0x7c641b00,
	0x7c701a00,
	0x7c601e00,
	0x8c101800,
	0x08000279,
	0x506f1b00,
	0x50f3fa00,
	0x0600e00e,
	0x8c101b00,
	0x08000282,
	0x7c940800,
	0x506f1b00,
	0x50ebfa00,
	0x5063bb00,
	0x50f7fa00,
	0x8c101800,
	0x08000279,
	0x506f1b00,
	0x50ebfa00,
	0x52639b00,
	0x7ca80500,
	0x52f3fa00,

	0x52e39d00,
	0x7ca80500,
	0x95800000,
	0x97800100,
	0x54739c00,
	0x0600c007,
	0x8c141800,
	0x7c900000,
	0x54f71e00,
	0x99600000,
	0x7c800500,
	0x6663dd08,
	0x7ca00500,

	0x0c000000,


#define CF_setupPtrs_adr 697
	0x4c7fff00,
	0x95800000,
	0x94800000,
	0x4c042100,
	0x80040004,
	0x80840003,
	0x81040004,
	0x81840002,
	0x97800100,
	0x0c000000,


#define CF_mulx_adr 707
	0x84004000,
	0x080002b9,
	0x8c041100,
	0x4c084200,
	0x0600c001,
	0x7ca80300,

	0x97800100,
	0x0600c004,
	0x8c0c1c00,
	0x95000000,
	0x0800028b,
	0x95800000,

	0x97800100,
	0x95800000,
	0x0600c001,
	0x90740800,

	0x97800100,
	0x95800000,
	0x0c000000,


#define CF_mul1_exp_adr 726
	0x8c041100,
	0x4c084200,
	0x0600c001,
	0x7ca80300,

	0x97800100,
	0x80080001,
	0x0600c003,
	0x95800000,
	0x0800028b,
	0x4c084200,

	0x97800100,
	0x95800000,
	0x56084200,
	0x0600c003,
	0x8c041800,
	0x7c8c0800,
	0x5e804300,

	0x97800100,
	0x95800000,
	0x540c6300,
	0x0600c006,
	0x8c041800,
	0x7c8c0800,
	0x548c6200,
	0x66084308,
	0x90740300,
	0xfc000000,

	0x97800100,
	0x95800000,
	0x0c000000,


#define CF_mul1_adr 756
	0x84004000,
	0x080002b9,
	0x080002d6,
	0x0c000000,


#define CF_sqrx_exp_adr 760
	0x84004020,
	0x95800000,
	0x8c041100,
	0x4c084200,
	0x0600c001,
	0x7ca80300,

	0x97800100,
	0x0600c004,
	0x8c0c1c00,
	0x95000000,
	0x0800028b,
	0x95800000,

	0x97800100,
	0x95800000,
	0x0600c001,
	0x90740800,

	0x97800100,
	0x95800000,
	0x0c000000,


#define CF_mulx_exp_adr 779
	0x84004040,
	0x95800000,
	0x8c041100,
	0x4c084200,
	0x0600c001,
	0x7ca80300,

	0x97800100,
	0x0600c004,
	0x8c0c1c00,
	0x95000000,
	0x0800028b,
	0x95800000,

	0x97800100,
	0x0c000000,


#define CF_modexp_adr 793
	0x080002c3,
	0x84004060,
	0x95800000,
	0x54084200,
	0x0600c004,
	0xfc000000,
	0x8c0c1800,
	0x54885f00,
	0x90740300,

	0xfc000000,
	0x840820c0,
	0x400c0298,
	0x48084000,
	0x40080200,
	0x50086201,
	0x94800200,
	0x06000015,
	0x080002f8,
	0x0800030b,
	0x84004060,
	0x95800000,
	0x99080000,
	0x54084200,
	0x0600c004,
	0x99080000,
	0x8c0c1400,
	0x50884200,
	0x90700300,

	0x0600c008,
	0x99080000,
	0x8c041500,
	0x90540300,
	0x7c8c0800,
	0x7c000200,
	0x99080000,
	0x64086008,
	0x90740300,

	0xfc000000,

	0x84004060,
	0x95800000,
	0x080002d6,
	0x0c000000,


#define CF_modload_adr 835
	0x4c7fff00,
	0x84004000,
	0x95800000,
	0x94800000,
	0x8000001c,
	0x8080001d,
	0x97800000,
	0x8c001000,
	0x08000239,
	0x90440100,
	0x08000251,
	0x0c000000,

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
	uint32_t mod[64];
	uint32_t dInv[8];
	uint32_t RR[64];
	uint32_t in[64];
	uint32_t exp[64];
	uint32_t out[64];
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
