/* Copyright 2015, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

/* This code is mostly taken from the ref10 version of Ed25519 in SUPERCOP
 * 20141124 (http://bench.cr.yp.to/supercop.html). That code is released as
 * public domain but this file has the ISC license just to keep licencing
 * simple.
 *
 * The field functions are shared by Ed25519 and X25519 where possible. */

#include "curve25519.h"
#include "util.h"

/*
 * fe means field element. Here the field is \Z/(2^255-19). An element t,
 * entries t[0]...t[9], represents the integer t[0]+2^26 t[1]+2^51 t[2]+2^77
 * t[3]+2^102 t[4]+...+2^230 t[9]. Bounds on each t[i] vary depending on
 * context.
 */
typedef int32_t fe[10];

static const int64_t kBottom25Bits = INT64_C(0x1ffffff);
static const int64_t kBottom26Bits = INT64_C(0x3ffffff);
static const int64_t kTop39Bits = INT64_C(0xfffffffffe000000);
static const int64_t kTop38Bits = INT64_C(0xfffffffffc000000);

static uint64_t load_3(const uint8_t *in) {
  uint64_t result;
  result = (uint64_t)in[0];
  result |= ((uint64_t)in[1]) << 8;
  result |= ((uint64_t)in[2]) << 16;
  return result;
}

static uint64_t load_4(const uint8_t *in) {
  uint64_t result;
  result = (uint64_t)in[0];
  result |= ((uint64_t)in[1]) << 8;
  result |= ((uint64_t)in[2]) << 16;
  result |= ((uint64_t)in[3]) << 24;
  return result;
}

static void fe_frombytes(fe h, const uint8_t *s) {
  /* Ignores top bit of h. */
  int64_t h0 = load_4(s);
  int64_t h1 = load_3(s + 4) << 6;
  int64_t h2 = load_3(s + 7) << 5;
  int64_t h3 = load_3(s + 10) << 3;
  int64_t h4 = load_3(s + 13) << 2;
  int64_t h5 = load_4(s + 16);
  int64_t h6 = load_3(s + 20) << 7;
  int64_t h7 = load_3(s + 23) << 5;
  int64_t h8 = load_3(s + 26) << 4;
  int64_t h9 = (load_3(s + 29) & 8388607) << 2;
  int64_t carry0;
  int64_t carry1;
  int64_t carry2;
  int64_t carry3;
  int64_t carry4;
  int64_t carry5;
  int64_t carry6;
  int64_t carry7;
  int64_t carry8;
  int64_t carry9;

  carry9 = h9 + BIT(24); h0 += (carry9 >> 25) * 19; h9 -= carry9 & kTop39Bits;
  carry1 = h1 + BIT(24); h2 += carry1 >> 25; h1 -= carry1 & kTop39Bits;
  carry3 = h3 + BIT(24); h4 += carry3 >> 25; h3 -= carry3 & kTop39Bits;
  carry5 = h5 + BIT(24); h6 += carry5 >> 25; h5 -= carry5 & kTop39Bits;
  carry7 = h7 + BIT(24); h8 += carry7 >> 25; h7 -= carry7 & kTop39Bits;

  carry0 = h0 + BIT(25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;
  carry2 = h2 + BIT(25); h3 += carry2 >> 26; h2 -= carry2 & kTop38Bits;
  carry4 = h4 + BIT(25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;
  carry6 = h6 + BIT(25); h7 += carry6 >> 26; h6 -= carry6 & kTop38Bits;
  carry8 = h8 + BIT(25); h9 += carry8 >> 26; h8 -= carry8 & kTop38Bits;

  h[0] = h0;
  h[1] = h1;
  h[2] = h2;
  h[3] = h3;
  h[4] = h4;
  h[5] = h5;
  h[6] = h6;
  h[7] = h7;
  h[8] = h8;
  h[9] = h9;
}

/* Preconditions:
 *  |h| bounded by 1.1*2^26,1.1*2^25,1.1*2^26,1.1*2^25,etc.
 *
 * Write p=2^255-19; q=floor(h/p).
 * Basic claim: q = floor(2^(-255)(h + 19 2^(-25)h9 + 2^(-1))).
 *
 * Proof:
 *   Have |h|<=p so |q|<=1 so |19^2 2^(-255) q|<1/4.
 *   Also have |h-2^230 h9|<2^231 so |19 2^(-255)(h-2^230 h9)|<1/4.
 *
 *   Write y=2^(-1)-19^2 2^(-255)q-19 2^(-255)(h-2^230 h9).
 *   Then 0<y<1.
 *
 *   Write r=h-pq.
 *   Have 0<=r<=p-1=2^255-20.
 *   Thus 0<=r+19(2^-255)r<r+19(2^-255)2^255<=2^255-1.
 *
 *   Write x=r+19(2^-255)r+y.
 *   Then 0<x<2^255 so floor(2^(-255)x) = 0 so floor(q+2^(-255)x) = q.
 *
 *   Have q+2^(-255)x = 2^(-255)(h + 19 2^(-25) h9 + 2^(-1))
 *   so floor(2^(-255)(h + 19 2^(-25) h9 + 2^(-1))) = q. */
static void fe_tobytes(uint8_t *s, const fe h) {
  int32_t h0 = h[0];
  int32_t h1 = h[1];
  int32_t h2 = h[2];
  int32_t h3 = h[3];
  int32_t h4 = h[4];
  int32_t h5 = h[5];
  int32_t h6 = h[6];
  int32_t h7 = h[7];
  int32_t h8 = h[8];
  int32_t h9 = h[9];
  int32_t q;

  q = (19 * h9 + (((int32_t) 1) << 24)) >> 25;
  q = (h0 + q) >> 26;
  q = (h1 + q) >> 25;
  q = (h2 + q) >> 26;
  q = (h3 + q) >> 25;
  q = (h4 + q) >> 26;
  q = (h5 + q) >> 25;
  q = (h6 + q) >> 26;
  q = (h7 + q) >> 25;
  q = (h8 + q) >> 26;
  q = (h9 + q) >> 25;

  /* Goal: Output h-(2^255-19)q, which is between 0 and 2^255-20. */
  h0 += 19 * q;
  /* Goal: Output h-2^255 q, which is between 0 and 2^255-20. */

  h1 += h0 >> 26; h0 &= kBottom26Bits;
  h2 += h1 >> 25; h1 &= kBottom25Bits;
  h3 += h2 >> 26; h2 &= kBottom26Bits;
  h4 += h3 >> 25; h3 &= kBottom25Bits;
  h5 += h4 >> 26; h4 &= kBottom26Bits;
  h6 += h5 >> 25; h5 &= kBottom25Bits;
  h7 += h6 >> 26; h6 &= kBottom26Bits;
  h8 += h7 >> 25; h7 &= kBottom25Bits;
  h9 += h8 >> 26; h8 &= kBottom26Bits;
                  h9 &= kBottom25Bits;
                  /* h10 = carry9 */

  /* Goal: Output h0+...+2^255 h10-2^255 q, which is between 0 and 2^255-20.
   * Have h0+...+2^230 h9 between 0 and 2^255-1;
   * evidently 2^255 h10-2^255 q = 0.
   * Goal: Output h0+...+2^230 h9.  */

  s[0] = h0 >> 0;
  s[1] = h0 >> 8;
  s[2] = h0 >> 16;
  s[3] = (h0 >> 24) | ((uint32_t)(h1) << 2);
  s[4] = h1 >> 6;
  s[5] = h1 >> 14;
  s[6] = (h1 >> 22) | ((uint32_t)(h2) << 3);
  s[7] = h2 >> 5;
  s[8] = h2 >> 13;
  s[9] = (h2 >> 21) | ((uint32_t)(h3) << 5);
  s[10] = h3 >> 3;
  s[11] = h3 >> 11;
  s[12] = (h3 >> 19) | ((uint32_t)(h4) << 6);
  s[13] = h4 >> 2;
  s[14] = h4 >> 10;
  s[15] = h4 >> 18;
  s[16] = h5 >> 0;
  s[17] = h5 >> 8;
  s[18] = h5 >> 16;
  s[19] = (h5 >> 24) | ((uint32_t)(h6) << 1);
  s[20] = h6 >> 7;
  s[21] = h6 >> 15;
  s[22] = (h6 >> 23) | ((uint32_t)(h7) << 3);
  s[23] = h7 >> 5;
  s[24] = h7 >> 13;
  s[25] = (h7 >> 21) | ((uint32_t)(h8) << 4);
  s[26] = h8 >> 4;
  s[27] = h8 >> 12;
  s[28] = (h8 >> 20) | ((uint32_t)(h9) << 6);
  s[29] = h9 >> 2;
  s[30] = h9 >> 10;
  s[31] = h9 >> 18;
}

/* h = f */
static void fe_copy(fe h, const fe f) {
  memmove(h, f, sizeof(int32_t) * 10);
}

/* h = 0 */
static void fe_0(fe h) { memset(h, 0, sizeof(int32_t) * 10); }

/* h = 1 */
static void fe_1(fe h) {
  memset(h, 0, sizeof(int32_t) * 10);
  h[0] = 1;
}

/* h = f + g
 * Can overlap h with f or g.
 *
 * Preconditions:
 *    |f| bounded by 1.1*2^25,1.1*2^24,1.1*2^25,1.1*2^24,etc.
 *    |g| bounded by 1.1*2^25,1.1*2^24,1.1*2^25,1.1*2^24,etc.
 *
 * Postconditions:
 *    |h| bounded by 1.1*2^26,1.1*2^25,1.1*2^26,1.1*2^25,etc. */
static void fe_add(fe h, const fe f, const fe g) {
  unsigned i;
  for (i = 0; i < 10; i++) {
    h[i] = f[i] + g[i];
  }
}

/* h = f - g
 * Can overlap h with f or g.
 *
 * Preconditions:
 *    |f| bounded by 1.1*2^25,1.1*2^24,1.1*2^25,1.1*2^24,etc.
 *    |g| bounded by 1.1*2^25,1.1*2^24,1.1*2^25,1.1*2^24,etc.
 *
 * Postconditions:
 *    |h| bounded by 1.1*2^26,1.1*2^25,1.1*2^26,1.1*2^25,etc. */
static void fe_sub(fe h, const fe f, const fe g) {
  unsigned i;
  for (i = 0; i < 10; i++) {
    h[i] = f[i] - g[i];
  }
}

/* h = f * g
 * Can overlap h with f or g.
 *
 * Preconditions:
 *    |f| bounded by 1.65*2^26,1.65*2^25,1.65*2^26,1.65*2^25,etc.
 *    |g| bounded by 1.65*2^26,1.65*2^25,1.65*2^26,1.65*2^25,etc.
 *
 * Postconditions:
 *    |h| bounded by 1.01*2^25,1.01*2^24,1.01*2^25,1.01*2^24,etc.
 *
 * Notes on implementation strategy:
 *
 * Using schoolbook multiplication.
 * Karatsuba would save a little in some cost models.
 *
 * Most multiplications by 2 and 19 are 32-bit precomputations;
 * cheaper than 64-bit postcomputations.
 *
 * There is one remaining multiplication by 19 in the carry chain;
 * one *19 precomputation can be merged into this,
 * but the resulting data flow is considerably less clean.
 *
 * There are 12 carries below.
 * 10 of them are 2-way parallelizable and vectorizable.
 * Can get away with 11 carries, but then data flow is much deeper.
 *
 * With tighter constraints on inputs can squeeze carries into int32. */
static void fe_mul(fe h, const fe f, const fe g) {
  int32_t f0 = f[0];
  int32_t f1 = f[1];
  int32_t f2 = f[2];
  int32_t f3 = f[3];
  int32_t f4 = f[4];
  int32_t f5 = f[5];
  int32_t f6 = f[6];
  int32_t f7 = f[7];
  int32_t f8 = f[8];
  int32_t f9 = f[9];
  int32_t g0 = g[0];
  int32_t g1 = g[1];
  int32_t g2 = g[2];
  int32_t g3 = g[3];
  int32_t g4 = g[4];
  int32_t g5 = g[5];
  int32_t g6 = g[6];
  int32_t g7 = g[7];
  int32_t g8 = g[8];
  int32_t g9 = g[9];
  int32_t g1_19 = 19 * g1; /* 1.959375*2^29 */
  int32_t g2_19 = 19 * g2; /* 1.959375*2^30; still ok */
  int32_t g3_19 = 19 * g3;
  int32_t g4_19 = 19 * g4;
  int32_t g5_19 = 19 * g5;
  int32_t g6_19 = 19 * g6;
  int32_t g7_19 = 19 * g7;
  int32_t g8_19 = 19 * g8;
  int32_t g9_19 = 19 * g9;
  int32_t f1_2 = 2 * f1;
  int32_t f3_2 = 2 * f3;
  int32_t f5_2 = 2 * f5;
  int32_t f7_2 = 2 * f7;
  int32_t f9_2 = 2 * f9;
  int64_t f0g0    = f0   * (int64_t) g0;
  int64_t f0g1    = f0   * (int64_t) g1;
  int64_t f0g2    = f0   * (int64_t) g2;
  int64_t f0g3    = f0   * (int64_t) g3;
  int64_t f0g4    = f0   * (int64_t) g4;
  int64_t f0g5    = f0   * (int64_t) g5;
  int64_t f0g6    = f0   * (int64_t) g6;
  int64_t f0g7    = f0   * (int64_t) g7;
  int64_t f0g8    = f0   * (int64_t) g8;
  int64_t f0g9    = f0   * (int64_t) g9;
  int64_t f1g0    = f1   * (int64_t) g0;
  int64_t f1g1_2  = f1_2 * (int64_t) g1;
  int64_t f1g2    = f1   * (int64_t) g2;
  int64_t f1g3_2  = f1_2 * (int64_t) g3;
  int64_t f1g4    = f1   * (int64_t) g4;
  int64_t f1g5_2  = f1_2 * (int64_t) g5;
  int64_t f1g6    = f1   * (int64_t) g6;
  int64_t f1g7_2  = f1_2 * (int64_t) g7;
  int64_t f1g8    = f1   * (int64_t) g8;
  int64_t f1g9_38 = f1_2 * (int64_t) g9_19;
  int64_t f2g0    = f2   * (int64_t) g0;
  int64_t f2g1    = f2   * (int64_t) g1;
  int64_t f2g2    = f2   * (int64_t) g2;
  int64_t f2g3    = f2   * (int64_t) g3;
  int64_t f2g4    = f2   * (int64_t) g4;
  int64_t f2g5    = f2   * (int64_t) g5;
  int64_t f2g6    = f2   * (int64_t) g6;
  int64_t f2g7    = f2   * (int64_t) g7;
  int64_t f2g8_19 = f2   * (int64_t) g8_19;
  int64_t f2g9_19 = f2   * (int64_t) g9_19;
  int64_t f3g0    = f3   * (int64_t) g0;
  int64_t f3g1_2  = f3_2 * (int64_t) g1;
  int64_t f3g2    = f3   * (int64_t) g2;
  int64_t f3g3_2  = f3_2 * (int64_t) g3;
  int64_t f3g4    = f3   * (int64_t) g4;
  int64_t f3g5_2  = f3_2 * (int64_t) g5;
  int64_t f3g6    = f3   * (int64_t) g6;
  int64_t f3g7_38 = f3_2 * (int64_t) g7_19;
  int64_t f3g8_19 = f3   * (int64_t) g8_19;
  int64_t f3g9_38 = f3_2 * (int64_t) g9_19;
  int64_t f4g0    = f4   * (int64_t) g0;
  int64_t f4g1    = f4   * (int64_t) g1;
  int64_t f4g2    = f4   * (int64_t) g2;
  int64_t f4g3    = f4   * (int64_t) g3;
  int64_t f4g4    = f4   * (int64_t) g4;
  int64_t f4g5    = f4   * (int64_t) g5;
  int64_t f4g6_19 = f4   * (int64_t) g6_19;
  int64_t f4g7_19 = f4   * (int64_t) g7_19;
  int64_t f4g8_19 = f4   * (int64_t) g8_19;
  int64_t f4g9_19 = f4   * (int64_t) g9_19;
  int64_t f5g0    = f5   * (int64_t) g0;
  int64_t f5g1_2  = f5_2 * (int64_t) g1;
  int64_t f5g2    = f5   * (int64_t) g2;
  int64_t f5g3_2  = f5_2 * (int64_t) g3;
  int64_t f5g4    = f5   * (int64_t) g4;
  int64_t f5g5_38 = f5_2 * (int64_t) g5_19;
  int64_t f5g6_19 = f5   * (int64_t) g6_19;
  int64_t f5g7_38 = f5_2 * (int64_t) g7_19;
  int64_t f5g8_19 = f5   * (int64_t) g8_19;
  int64_t f5g9_38 = f5_2 * (int64_t) g9_19;
  int64_t f6g0    = f6   * (int64_t) g0;
  int64_t f6g1    = f6   * (int64_t) g1;
  int64_t f6g2    = f6   * (int64_t) g2;
  int64_t f6g3    = f6   * (int64_t) g3;
  int64_t f6g4_19 = f6   * (int64_t) g4_19;
  int64_t f6g5_19 = f6   * (int64_t) g5_19;
  int64_t f6g6_19 = f6   * (int64_t) g6_19;
  int64_t f6g7_19 = f6   * (int64_t) g7_19;
  int64_t f6g8_19 = f6   * (int64_t) g8_19;
  int64_t f6g9_19 = f6   * (int64_t) g9_19;
  int64_t f7g0    = f7   * (int64_t) g0;
  int64_t f7g1_2  = f7_2 * (int64_t) g1;
  int64_t f7g2    = f7   * (int64_t) g2;
  int64_t f7g3_38 = f7_2 * (int64_t) g3_19;
  int64_t f7g4_19 = f7   * (int64_t) g4_19;
  int64_t f7g5_38 = f7_2 * (int64_t) g5_19;
  int64_t f7g6_19 = f7   * (int64_t) g6_19;
  int64_t f7g7_38 = f7_2 * (int64_t) g7_19;
  int64_t f7g8_19 = f7   * (int64_t) g8_19;
  int64_t f7g9_38 = f7_2 * (int64_t) g9_19;
  int64_t f8g0    = f8   * (int64_t) g0;
  int64_t f8g1    = f8   * (int64_t) g1;
  int64_t f8g2_19 = f8   * (int64_t) g2_19;
  int64_t f8g3_19 = f8   * (int64_t) g3_19;
  int64_t f8g4_19 = f8   * (int64_t) g4_19;
  int64_t f8g5_19 = f8   * (int64_t) g5_19;
  int64_t f8g6_19 = f8   * (int64_t) g6_19;
  int64_t f8g7_19 = f8   * (int64_t) g7_19;
  int64_t f8g8_19 = f8   * (int64_t) g8_19;
  int64_t f8g9_19 = f8   * (int64_t) g9_19;
  int64_t f9g0    = f9   * (int64_t) g0;
  int64_t f9g1_38 = f9_2 * (int64_t) g1_19;
  int64_t f9g2_19 = f9   * (int64_t) g2_19;
  int64_t f9g3_38 = f9_2 * (int64_t) g3_19;
  int64_t f9g4_19 = f9   * (int64_t) g4_19;
  int64_t f9g5_38 = f9_2 * (int64_t) g5_19;
  int64_t f9g6_19 = f9   * (int64_t) g6_19;
  int64_t f9g7_38 = f9_2 * (int64_t) g7_19;
  int64_t f9g8_19 = f9   * (int64_t) g8_19;
  int64_t f9g9_38 = f9_2 * (int64_t) g9_19;
  int64_t h0 = f0g0+f1g9_38+f2g8_19+f3g7_38+f4g6_19+f5g5_38+f6g4_19+f7g3_38+f8g2_19+f9g1_38;
  int64_t h1 = f0g1+f1g0   +f2g9_19+f3g8_19+f4g7_19+f5g6_19+f6g5_19+f7g4_19+f8g3_19+f9g2_19;
  int64_t h2 = f0g2+f1g1_2 +f2g0   +f3g9_38+f4g8_19+f5g7_38+f6g6_19+f7g5_38+f8g4_19+f9g3_38;
  int64_t h3 = f0g3+f1g2   +f2g1   +f3g0   +f4g9_19+f5g8_19+f6g7_19+f7g6_19+f8g5_19+f9g4_19;
  int64_t h4 = f0g4+f1g3_2 +f2g2   +f3g1_2 +f4g0   +f5g9_38+f6g8_19+f7g7_38+f8g6_19+f9g5_38;
  int64_t h5 = f0g5+f1g4   +f2g3   +f3g2   +f4g1   +f5g0   +f6g9_19+f7g8_19+f8g7_19+f9g6_19;
  int64_t h6 = f0g6+f1g5_2 +f2g4   +f3g3_2 +f4g2   +f5g1_2 +f6g0   +f7g9_38+f8g8_19+f9g7_38;
  int64_t h7 = f0g7+f1g6   +f2g5   +f3g4   +f4g3   +f5g2   +f6g1   +f7g0   +f8g9_19+f9g8_19;
  int64_t h8 = f0g8+f1g7_2 +f2g6   +f3g5_2 +f4g4   +f5g3_2 +f6g2   +f7g1_2 +f8g0   +f9g9_38;
  int64_t h9 = f0g9+f1g8   +f2g7   +f3g6   +f4g5   +f5g4   +f6g3   +f7g2   +f8g1   +f9g0   ;
  int64_t carry0;
  int64_t carry1;
  int64_t carry2;
  int64_t carry3;
  int64_t carry4;
  int64_t carry5;
  int64_t carry6;
  int64_t carry7;
  int64_t carry8;
  int64_t carry9;

  /* |h0| <= (1.65*1.65*2^52*(1+19+19+19+19)+1.65*1.65*2^50*(38+38+38+38+38))
   *   i.e. |h0| <= 1.4*2^60; narrower ranges for h2, h4, h6, h8
   * |h1| <= (1.65*1.65*2^51*(1+1+19+19+19+19+19+19+19+19))
   *   i.e. |h1| <= 1.7*2^59; narrower ranges for h3, h5, h7, h9 */

  carry0 = h0 + BIT(25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;
  carry4 = h4 + BIT(25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;
  /* |h0| <= 2^25 */
  /* |h4| <= 2^25 */
  /* |h1| <= 1.71*2^59 */
  /* |h5| <= 1.71*2^59 */

  carry1 = h1 + BIT(24); h2 += carry1 >> 25; h1 -= carry1 & kTop39Bits;
  carry5 = h5 + BIT(24); h6 += carry5 >> 25; h5 -= carry5 & kTop39Bits;
  /* |h1| <= 2^24; from now on fits into int32 */
  /* |h5| <= 2^24; from now on fits into int32 */
  /* |h2| <= 1.41*2^60 */
  /* |h6| <= 1.41*2^60 */

  carry2 = h2 + BIT(25); h3 += carry2 >> 26; h2 -= carry2 & kTop38Bits;
  carry6 = h6 + BIT(25); h7 += carry6 >> 26; h6 -= carry6 & kTop38Bits;
  /* |h2| <= 2^25; from now on fits into int32 unchanged */
  /* |h6| <= 2^25; from now on fits into int32 unchanged */
  /* |h3| <= 1.71*2^59 */
  /* |h7| <= 1.71*2^59 */

  carry3 = h3 + BIT(24); h4 += carry3 >> 25; h3 -= carry3 & kTop39Bits;
  carry7 = h7 + BIT(24); h8 += carry7 >> 25; h7 -= carry7 & kTop39Bits;
  /* |h3| <= 2^24; from now on fits into int32 unchanged */
  /* |h7| <= 2^24; from now on fits into int32 unchanged */
  /* |h4| <= 1.72*2^34 */
  /* |h8| <= 1.41*2^60 */

  carry4 = h4 + BIT(25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;
  carry8 = h8 + BIT(25); h9 += carry8 >> 26; h8 -= carry8 & kTop38Bits;
  /* |h4| <= 2^25; from now on fits into int32 unchanged */
  /* |h8| <= 2^25; from now on fits into int32 unchanged */
  /* |h5| <= 1.01*2^24 */
  /* |h9| <= 1.71*2^59 */

  carry9 = h9 + BIT(24); h0 += (carry9 >> 25) * 19; h9 -= carry9 & kTop39Bits;
  /* |h9| <= 2^24; from now on fits into int32 unchanged */
  /* |h0| <= 1.1*2^39 */

  carry0 = h0 + BIT(25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;
  /* |h0| <= 2^25; from now on fits into int32 unchanged */
  /* |h1| <= 1.01*2^24 */

  h[0] = h0;
  h[1] = h1;
  h[2] = h2;
  h[3] = h3;
  h[4] = h4;
  h[5] = h5;
  h[6] = h6;
  h[7] = h7;
  h[8] = h8;
  h[9] = h9;
}

/* h = f * f
 * Can overlap h with f.
 *
 * Preconditions:
 *    |f| bounded by 1.65*2^26,1.65*2^25,1.65*2^26,1.65*2^25,etc.
 *
 * Postconditions:
 *    |h| bounded by 1.01*2^25,1.01*2^24,1.01*2^25,1.01*2^24,etc.
 *
 * See fe_mul.c for discussion of implementation strategy. */
static void fe_sq(fe h, const fe f) {
  int32_t f0 = f[0];
  int32_t f1 = f[1];
  int32_t f2 = f[2];
  int32_t f3 = f[3];
  int32_t f4 = f[4];
  int32_t f5 = f[5];
  int32_t f6 = f[6];
  int32_t f7 = f[7];
  int32_t f8 = f[8];
  int32_t f9 = f[9];
  int32_t f0_2 = 2 * f0;
  int32_t f1_2 = 2 * f1;
  int32_t f2_2 = 2 * f2;
  int32_t f3_2 = 2 * f3;
  int32_t f4_2 = 2 * f4;
  int32_t f5_2 = 2 * f5;
  int32_t f6_2 = 2 * f6;
  int32_t f7_2 = 2 * f7;
  int32_t f5_38 = 38 * f5; /* 1.959375*2^30 */
  int32_t f6_19 = 19 * f6; /* 1.959375*2^30 */
  int32_t f7_38 = 38 * f7; /* 1.959375*2^30 */
  int32_t f8_19 = 19 * f8; /* 1.959375*2^30 */
  int32_t f9_38 = 38 * f9; /* 1.959375*2^30 */
  int64_t f0f0    = f0   * (int64_t) f0;
  int64_t f0f1_2  = f0_2 * (int64_t) f1;
  int64_t f0f2_2  = f0_2 * (int64_t) f2;
  int64_t f0f3_2  = f0_2 * (int64_t) f3;
  int64_t f0f4_2  = f0_2 * (int64_t) f4;
  int64_t f0f5_2  = f0_2 * (int64_t) f5;
  int64_t f0f6_2  = f0_2 * (int64_t) f6;
  int64_t f0f7_2  = f0_2 * (int64_t) f7;
  int64_t f0f8_2  = f0_2 * (int64_t) f8;
  int64_t f0f9_2  = f0_2 * (int64_t) f9;
  int64_t f1f1_2  = f1_2 * (int64_t) f1;
  int64_t f1f2_2  = f1_2 * (int64_t) f2;
  int64_t f1f3_4  = f1_2 * (int64_t) f3_2;
  int64_t f1f4_2  = f1_2 * (int64_t) f4;
  int64_t f1f5_4  = f1_2 * (int64_t) f5_2;
  int64_t f1f6_2  = f1_2 * (int64_t) f6;
  int64_t f1f7_4  = f1_2 * (int64_t) f7_2;
  int64_t f1f8_2  = f1_2 * (int64_t) f8;
  int64_t f1f9_76 = f1_2 * (int64_t) f9_38;
  int64_t f2f2    = f2   * (int64_t) f2;
  int64_t f2f3_2  = f2_2 * (int64_t) f3;
  int64_t f2f4_2  = f2_2 * (int64_t) f4;
  int64_t f2f5_2  = f2_2 * (int64_t) f5;
  int64_t f2f6_2  = f2_2 * (int64_t) f6;
  int64_t f2f7_2  = f2_2 * (int64_t) f7;
  int64_t f2f8_38 = f2_2 * (int64_t) f8_19;
  int64_t f2f9_38 = f2   * (int64_t) f9_38;
  int64_t f3f3_2  = f3_2 * (int64_t) f3;
  int64_t f3f4_2  = f3_2 * (int64_t) f4;
  int64_t f3f5_4  = f3_2 * (int64_t) f5_2;
  int64_t f3f6_2  = f3_2 * (int64_t) f6;
  int64_t f3f7_76 = f3_2 * (int64_t) f7_38;
  int64_t f3f8_38 = f3_2 * (int64_t) f8_19;
  int64_t f3f9_76 = f3_2 * (int64_t) f9_38;
  int64_t f4f4    = f4   * (int64_t) f4;
  int64_t f4f5_2  = f4_2 * (int64_t) f5;
  int64_t f4f6_38 = f4_2 * (int64_t) f6_19;
  int64_t f4f7_38 = f4   * (int64_t) f7_38;
  int64_t f4f8_38 = f4_2 * (int64_t) f8_19;
  int64_t f4f9_38 = f4   * (int64_t) f9_38;
  int64_t f5f5_38 = f5   * (int64_t) f5_38;
  int64_t f5f6_38 = f5_2 * (int64_t) f6_19;
  int64_t f5f7_76 = f5_2 * (int64_t) f7_38;
  int64_t f5f8_38 = f5_2 * (int64_t) f8_19;
  int64_t f5f9_76 = f5_2 * (int64_t) f9_38;
  int64_t f6f6_19 = f6   * (int64_t) f6_19;
  int64_t f6f7_38 = f6   * (int64_t) f7_38;
  int64_t f6f8_38 = f6_2 * (int64_t) f8_19;
  int64_t f6f9_38 = f6   * (int64_t) f9_38;
  int64_t f7f7_38 = f7   * (int64_t) f7_38;
  int64_t f7f8_38 = f7_2 * (int64_t) f8_19;
  int64_t f7f9_76 = f7_2 * (int64_t) f9_38;
  int64_t f8f8_19 = f8   * (int64_t) f8_19;
  int64_t f8f9_38 = f8   * (int64_t) f9_38;
  int64_t f9f9_38 = f9   * (int64_t) f9_38;
  int64_t h0 = f0f0  +f1f9_76+f2f8_38+f3f7_76+f4f6_38+f5f5_38;
  int64_t h1 = f0f1_2+f2f9_38+f3f8_38+f4f7_38+f5f6_38;
  int64_t h2 = f0f2_2+f1f1_2 +f3f9_76+f4f8_38+f5f7_76+f6f6_19;
  int64_t h3 = f0f3_2+f1f2_2 +f4f9_38+f5f8_38+f6f7_38;
  int64_t h4 = f0f4_2+f1f3_4 +f2f2   +f5f9_76+f6f8_38+f7f7_38;
  int64_t h5 = f0f5_2+f1f4_2 +f2f3_2 +f6f9_38+f7f8_38;
  int64_t h6 = f0f6_2+f1f5_4 +f2f4_2 +f3f3_2 +f7f9_76+f8f8_19;
  int64_t h7 = f0f7_2+f1f6_2 +f2f5_2 +f3f4_2 +f8f9_38;
  int64_t h8 = f0f8_2+f1f7_4 +f2f6_2 +f3f5_4 +f4f4   +f9f9_38;
  int64_t h9 = f0f9_2+f1f8_2 +f2f7_2 +f3f6_2 +f4f5_2;
  int64_t carry0;
  int64_t carry1;
  int64_t carry2;
  int64_t carry3;
  int64_t carry4;
  int64_t carry5;
  int64_t carry6;
  int64_t carry7;
  int64_t carry8;
  int64_t carry9;

  carry0 = h0 + BIT(25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;
  carry4 = h4 + BIT(25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;

  carry1 = h1 + BIT(24); h2 += carry1 >> 25; h1 -= carry1 & kTop39Bits;
  carry5 = h5 + BIT(24); h6 += carry5 >> 25; h5 -= carry5 & kTop39Bits;

  carry2 = h2 + BIT(25); h3 += carry2 >> 26; h2 -= carry2 & kTop38Bits;
  carry6 = h6 + BIT(25); h7 += carry6 >> 26; h6 -= carry6 & kTop38Bits;

  carry3 = h3 + BIT(24); h4 += carry3 >> 25; h3 -= carry3 & kTop39Bits;
  carry7 = h7 + BIT(24); h8 += carry7 >> 25; h7 -= carry7 & kTop39Bits;

  carry4 = h4 + BIT(25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;
  carry8 = h8 + BIT(25); h9 += carry8 >> 26; h8 -= carry8 & kTop38Bits;

  carry9 = h9 + BIT(24); h0 += (carry9 >> 25) * 19; h9 -= carry9 & kTop39Bits;

  carry0 = h0 + BIT(25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;

  h[0] = h0;
  h[1] = h1;
  h[2] = h2;
  h[3] = h3;
  h[4] = h4;
  h[5] = h5;
  h[6] = h6;
  h[7] = h7;
  h[8] = h8;
  h[9] = h9;
}

static void fe_invert(fe out, const fe z) {
  fe t0;
  fe t1;
  fe t2;
  fe t3;
  int i;

  fe_sq(t0, z);
  fe_sq(t1, t0);
  for (i = 1; i < 2; ++i) {
    fe_sq(t1, t1);
  }
  fe_mul(t1, z, t1);
  fe_mul(t0, t0, t1);
  fe_sq(t2, t0);
  fe_mul(t1, t1, t2);
  fe_sq(t2, t1);
  for (i = 1; i < 5; ++i) {
    fe_sq(t2, t2);
  }
  fe_mul(t1, t2, t1);
  fe_sq(t2, t1);
  for (i = 1; i < 10; ++i) {
    fe_sq(t2, t2);
  }
  fe_mul(t2, t2, t1);
  fe_sq(t3, t2);
  for (i = 1; i < 20; ++i) {
    fe_sq(t3, t3);
  }
  fe_mul(t2, t3, t2);
  fe_sq(t2, t2);
  for (i = 1; i < 10; ++i) {
    fe_sq(t2, t2);
  }
  fe_mul(t1, t2, t1);
  fe_sq(t2, t1);
  for (i = 1; i < 50; ++i) {
    fe_sq(t2, t2);
  }
  fe_mul(t2, t2, t1);
  fe_sq(t3, t2);
  for (i = 1; i < 100; ++i) {
    fe_sq(t3, t3);
  }
  fe_mul(t2, t3, t2);
  fe_sq(t2, t2);
  for (i = 1; i < 50; ++i) {
    fe_sq(t2, t2);
  }
  fe_mul(t1, t2, t1);
  fe_sq(t1, t1);
  for (i = 1; i < 5; ++i) {
    fe_sq(t1, t1);
  }
  fe_mul(out, t1, t0);
}

/* Replace (f,g) with (g,f) if b == 1;
 * replace (f,g) with (f,g) if b == 0.
 *
 * Preconditions: b in {0,1}. */
static void fe_cswap(fe f, fe g, unsigned int b) {
  unsigned i;
  b = 0-b;
  for (i = 0; i < 10; i++) {
    int32_t x = f[i] ^ g[i];
    x &= b;
    f[i] ^= x;
    g[i] ^= x;
  }
}

/* h = f * 121666
 * Can overlap h with f.
 *
 * Preconditions:
 *    |f| bounded by 1.1*2^26,1.1*2^25,1.1*2^26,1.1*2^25,etc.
 *
 * Postconditions:
 *    |h| bounded by 1.1*2^25,1.1*2^24,1.1*2^25,1.1*2^24,etc. */
static void fe_mul121666(fe h, fe f) {
  int32_t f0 = f[0];
  int32_t f1 = f[1];
  int32_t f2 = f[2];
  int32_t f3 = f[3];
  int32_t f4 = f[4];
  int32_t f5 = f[5];
  int32_t f6 = f[6];
  int32_t f7 = f[7];
  int32_t f8 = f[8];
  int32_t f9 = f[9];
  int64_t h0 = f0 * (int64_t) 121666;
  int64_t h1 = f1 * (int64_t) 121666;
  int64_t h2 = f2 * (int64_t) 121666;
  int64_t h3 = f3 * (int64_t) 121666;
  int64_t h4 = f4 * (int64_t) 121666;
  int64_t h5 = f5 * (int64_t) 121666;
  int64_t h6 = f6 * (int64_t) 121666;
  int64_t h7 = f7 * (int64_t) 121666;
  int64_t h8 = f8 * (int64_t) 121666;
  int64_t h9 = f9 * (int64_t) 121666;
  int64_t carry0;
  int64_t carry1;
  int64_t carry2;
  int64_t carry3;
  int64_t carry4;
  int64_t carry5;
  int64_t carry6;
  int64_t carry7;
  int64_t carry8;
  int64_t carry9;

  carry9 = h9 + BIT(24); h0 += (carry9 >> 25) * 19; h9 -= carry9 & kTop39Bits;
  carry1 = h1 + BIT(24); h2 += carry1 >> 25; h1 -= carry1 & kTop39Bits;
  carry3 = h3 + BIT(24); h4 += carry3 >> 25; h3 -= carry3 & kTop39Bits;
  carry5 = h5 + BIT(24); h6 += carry5 >> 25; h5 -= carry5 & kTop39Bits;
  carry7 = h7 + BIT(24); h8 += carry7 >> 25; h7 -= carry7 & kTop39Bits;

  carry0 = h0 + BIT(25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;
  carry2 = h2 + BIT(25); h3 += carry2 >> 26; h2 -= carry2 & kTop38Bits;
  carry4 = h4 + BIT(25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;
  carry6 = h6 + BIT(25); h7 += carry6 >> 26; h6 -= carry6 & kTop38Bits;
  carry8 = h8 + BIT(25); h9 += carry8 >> 26; h8 -= carry8 & kTop38Bits;

  h[0] = h0;
  h[1] = h1;
  h[2] = h2;
  h[3] = h3;
  h[4] = h4;
  h[5] = h5;
  h[6] = h6;
  h[7] = h7;
  h[8] = h8;
  h[9] = h9;
}

void x25519_scalar_mult(uint8_t out[32],
			const uint8_t scalar[32],
			const uint8_t point[32]) {
  fe x1, x2, z2, x3, z3, tmp0, tmp1;
  unsigned swap;
  int pos;

  uint8_t e[32];
  memcpy(e, scalar, 32);
  e[0] &= 248;
  e[31] &= 127;
  e[31] |= 64;
  fe_frombytes(x1, point);
  fe_1(x2);
  fe_0(z2);
  fe_copy(x3, x1);
  fe_1(z3);

  swap = 0;
  for (pos = 254; pos >= 0; --pos) {
    unsigned b = 1 & (e[pos / 8] >> (pos & 7));
    swap ^= b;
    fe_cswap(x2, x3, swap);
    fe_cswap(z2, z3, swap);
    swap = b;
    fe_sub(tmp0, x3, z3);
    fe_sub(tmp1, x2, z2);
    fe_add(x2, x2, z2);
    fe_add(z2, x3, z3);
    fe_mul(z3, tmp0, x2);
    fe_mul(z2, z2, tmp1);
    fe_sq(tmp0, tmp1);
    fe_sq(tmp1, x2);
    fe_add(x3, z3, z2);
    fe_sub(z2, z3, z2);
    fe_mul(x2, tmp1, tmp0);
    fe_sub(tmp1, tmp1, tmp0);
    fe_sq(z2, z2);
    fe_mul121666(z3, tmp1);
    fe_sq(x3, x3);
    fe_add(tmp0, tmp0, z3);
    fe_mul(z3, x1, z2);
    fe_mul(z2, tmp1, tmp0);
  }
  fe_cswap(x2, x3, swap);
  fe_cswap(z2, z3, swap);

  fe_invert(z2, z2);
  fe_mul(x2, x2, z2);
  fe_tobytes(out, x2);
}
