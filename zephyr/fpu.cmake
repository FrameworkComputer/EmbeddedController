# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#[[
Disclaimer: the following example is pieced together from
https://lemire.me/blog/2020/06/26/gcc-not-nearest/ along with other information
found in GCC documentation.

The following flags are needed to to ensure consistent FPU rounding in unit
tests. For example using GNU GCC 7.5 rounds down. Note that at the time of
writing, Clang 13.0.0 passes all the FPU unit tests without these flags.

Some of the sensor logic which requires FPU support is susceptible to rounding
errors. In GCC 7.5, as an example:

  double x = 50178230318.0;
  double y = 100000000000.0;
  double ratio = x/y;

In this example, we would expect ratio to be 0.50178230318. Instead, using a
64-bit float, it falls between:
* The floating-point number 0.501782303179999944 == 4519653187245114  * 2 ** -53
* The floating-point number 0.501782303180000055 == 4519653187245115  * 2 ** -53

The real mantissa using the same logic should be:
0.50178230318 = 4519653187245114.50011795456 * 2 ** -53

Since the mantissa's decimal is just over 0.5, it should stand to reason that
the correct solution is 0.501782303180000055. To force GCC to round correctly
we must set the following modes:
1. 'sse' - Generate SSE instructions.
2. 'fpmath=sse' - Use scalar floating-point instructions present in the SSE
   instruction set.
3. 'arch=pentium4' - Choose the Pentium4 because it's a stable choice that
   supports SSE and is known to work (there may be other good choices).
]]
if(DEFINED CONFIG_SOC_POSIX)
  zephyr_cc_option(-msse -mfpmath=sse -march=pentium4)
endif()
