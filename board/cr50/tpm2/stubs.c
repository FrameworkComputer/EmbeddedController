/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define        TPM_FAIL_C
#include "Global.h"
#include "CryptoEngine.h"

CRYPT_RESULT _cpri__C_2_2_KeyExchange(
  TPMS_ECC_POINT * outZ1,       //   OUT: a computed point
  TPMS_ECC_POINT * outZ2,       //   OUT: and optional second point
  TPM_ECC_CURVE curveId,        //   IN: the curve for the computations
  TPM_ALG_ID scheme,            //   IN: the key exchange scheme
  TPM2B_ECC_PARAMETER * dsA,    //   IN: static private TPM key
  TPM2B_ECC_PARAMETER * deA,    //   IN: ephemeral private TPM key
  TPMS_ECC_POINT * QsB,         //   IN: static public party B key
  TPMS_ECC_POINT * QeB          //   IN: ephemeral public party B key
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

CRYPT_RESULT _cpri__DrbgGetPutState(
  GET_PUT direction,
  int bufferSize,
  BYTE * buffer)
{
  /* This unction is not implemented in the TPM2 library either. */
  return CRYPT_SUCCESS;
}

CRYPT_RESULT _cpri__EccCommitCompute(
  TPMS_ECC_POINT * K,           //   OUT: [d]B or [r]Q
  TPMS_ECC_POINT * L,           //   OUT: [r]B
  TPMS_ECC_POINT * E,           //   OUT: [r]M
  TPM_ECC_CURVE curveId,        //   IN: the curve for the computations
  TPMS_ECC_POINT * M,           //   IN: M (optional)
  TPMS_ECC_POINT * B,           //   IN: B (optional)
  TPM2B_ECC_PARAMETER * d,      //   IN: d (required)
  TPM2B_ECC_PARAMETER * r       //   IN: the computed r value (required)
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

BOOL _cpri__Startup(
  void)
{
  /*
   * Below is the list of functions called by the TPM2 library from
   * _cpri__Startup().
   *
   *  _cpri__HashStartup() - not doing anything for now, maybe hw
   *               reinitialization is required?
   * _cpri__RsaStartup() - not sure what needs to be done in HW
   * _cpri__EccStartup() - not sure what needs to be done in HW
   * _cpri__SymStartup() - this function is emtpy in the TPM2 library
   *                implementation.
   */
  return 1;
}

CRYPT_RESULT _math__Div(
  const TPM2B * n,              //   IN: numerator
  const TPM2B * d,              //   IN: denominator
  TPM2B * q,                    //   OUT: quotient
  TPM2B * r                     //   OUT: remainder
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

void __assert_func(
  const char *file,
  int line,
  const char *func,
  const char *condition
)
{
  /*
   * TPM2 library invokes assert from a common wrapper, which first sets
   * global variables describing the failure point and then invokes the
   * assert() macro which ends up calling this function as defined by the gcc
   * toolchain.
   *
   * For some weird reason (or maybe this is a bug), s_FailFunction is defined
   * in the tpm2 library as a 32 bit int, but on a failure the name of the
   * failing function (its first four bytes) are copiied into this variable.
   *
   * TODO(vbendeb): investigate and fix TPM2 library assert handling.
   */
  ecprintf("Failure in %s, func %s, line %d:\n%s\n",
           file,
	   s_failFunction ? (const char *)&s_failFunction : func,
	   s_failLine ? s_failLine : line,
	   condition);
  while (1)
    ;                           /* Let the watchdog doo the rest. */
}

CRYPT_RESULT _cpri__InitCryptoUnits(
  FAIL_FUNCTION failFunction)
{
  return CRYPT_SUCCESS;
}
