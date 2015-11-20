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

UINT16 _cpri__CompleteHMAC(
  CPRI_HASH_STATE * hashState,  //   IN: the state of hash stack
  TPM2B * oPadKey,              //   IN: the HMAC key in oPad format
  UINT32 dOutSize,              //   IN: size of digest buffer
  BYTE * dOut                   //   OUT: hash digest
  )
{
  ecprintf("%s called\n", __func__);
  return -1;
}

CRYPT_RESULT _cpri__DecryptRSA(
  UINT32 * dOutSize,            //   OUT: the size of the decrypted data
  BYTE * dOut,                  //   OUT: the decrypted data
  RSA_KEY * key,                //   IN: the key to use for decryption
  TPM_ALG_ID padType,           //   IN: the type of padding
  UINT32 cInSize,               //   IN: the amount of data to decrypt
  BYTE * cIn,                   //   IN: the data to decrypt
  TPM_ALG_ID hashAlg,           //   IN: in case this is needed for the scheme
  const char *label             //   IN: in case it is needed for the scheme
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
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
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

UINT32 _cpri__EccGetCurveCount(
  void)
{
  ecprintf("%s called\n", __func__);
  return -1;
}

const ECC_CURVE *_cpri__EccGetParametersByCurveId(
  TPM_ECC_CURVE curveId         // IN: the curveID
  )
{
  ecprintf("%s called\n", __func__);
  return NULL;
}

BOOL _cpri__EccIsPointOnCurve(
  TPM_ECC_CURVE curveId,        // IN: the curve selector
  TPMS_ECC_POINT * Q            // IN: the point.
  )
{
  ecprintf("%s called\n", __func__);
  return 0;
}

CRYPT_RESULT _cpri__EccPointMultiply(
  TPMS_ECC_POINT * Rout,        //   OUT: the product point R
  TPM_ECC_CURVE curveId,        //   IN: the curve to use
  TPM2B_ECC_PARAMETER * dIn,    //   IN: value to multiply against the
  // curve generator
  TPMS_ECC_POINT * Qin,         //   IN: point Q
  TPM2B_ECC_PARAMETER * uIn     //   IN: scalar value for the multiplier of Q
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

CRYPT_RESULT _cpri__EncryptRSA(
  UINT32 * cOutSize,            //   OUT: the size of the encrypted data
  BYTE * cOut,                  //   OUT: the encrypted data
  RSA_KEY * key,                //   IN: the key to use for encryption
  TPM_ALG_ID padType,           //   IN: the type of padding
  UINT32 dInSize,               //   IN: the amount of data to encrypt
  BYTE * dIn,                   //   IN: the data to encrypt
  TPM_ALG_ID hashAlg,           //   IN: in case this is needed
  const char *label             //   IN: in case it is needed
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

CRYPT_RESULT _cpri__GenerateKeyEcc(
  TPMS_ECC_POINT * Qout,        //   OUT: the public point
  TPM2B_ECC_PARAMETER * dOut,   //   OUT: the private scalar
  TPM_ECC_CURVE curveId,        //   IN: the curve identifier
  TPM_ALG_ID hashAlg,           //   IN: hash algorithm to use in the key
  // generation process
  TPM2B * seed,                 //   IN: the seed to use
  const char *label,            //   IN: A label for the generation process.
  TPM2B * extra,                //   IN: Party 1 data for the KDF
  UINT32 * counter              //   IN/OUT: Counter value to allow KDF
  // iteration to be propagated across multiple functions
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

CRYPT_RESULT _cpri__GenerateKeyRSA(
  TPM2B * n,                    //   OUT: The public modulu
  TPM2B * p,                    //   OUT: One of the prime factors of n
  UINT16 keySizeInBits,         //   IN: Size of the public modulus in bit
  UINT32 e,                     //   IN: The public exponent
  TPM_ALG_ID hashAlg,           //   IN: hash algorithm to use in the key generation proce
  TPM2B * seed,                 //   IN: the seed to use
  const char *label,            //   IN: A label for the generation process.
  TPM2B * extra,                //   IN: Party 1 data for the KDF
  UINT32 * counter              //   IN/OUT: Counter value to allow KFD iteration to be
  //   propagated across multiple routine
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

UINT16 _cpri__GenerateSeededRandom(
  INT32 randomSize,             //   IN: the size of the request
  BYTE * random,                //   OUT: receives the data
  TPM_ALG_ID hashAlg,           //   IN: used by KDF version but not here
  TPM2B * seed,                 //   IN: the seed value
  const char *label,            //   IN: a label string (optional)
  TPM2B * partyU,               //   IN: other data (oprtional)
  TPM2B * partyV                //   IN: still more (optional)
  )
{
  ecprintf("%s called\n", __func__);
  return -1;
}

TPM_ECC_CURVE _cpri__GetCurveIdByIndex(
  UINT16 i)
{
  ecprintf("%s called\n", __func__);
  return TPM_ECC_NONE;
}

CRYPT_RESULT _cpri__GetEphemeralEcc(
  TPMS_ECC_POINT * Qout,        // OUT: the public point
  TPM2B_ECC_PARAMETER * dOut,   // OUT: the private scalar
  TPM_ECC_CURVE curveId         // IN: the curve for the key
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

INT16 _cpri__GetSymmetricBlockSize(
  TPM_ALG_ID symmetricAlg,      // IN: the symmetric algorithm
  UINT16 keySizeInBits          // IN: the key size
  )
{
  ecprintf("%s called\n", __func__);
  return -1;
}

UINT16 _cpri__KDFa(
  TPM_ALG_ID hashAlg,           //   IN: hash algorithm used in HMAC
  TPM2B * key,                  //   IN: HMAC key
  const char *label,            //   IN: a 0-byte terminated label used in KDF
  TPM2B * contextU,             //   IN: context U
  TPM2B * contextV,             //   IN: context V
  UINT32 sizeInBits,            //   IN: size of generated key in bit
  BYTE * keyStream,             //   OUT: key buffer
  UINT32 * counterInOut,        //   IN/OUT: caller may provide the iteration
  //   counter for incremental operations to
  //   avoid large intermediate buffers.
  BOOL once                     //   IN: TRUE if only one iteration is
  // performed FALSE if iteration count determined by "sizeInBits"
  )
{
  ecprintf("%s called\n", __func__);
  return -1;
}

UINT16 _cpri__KDFe(
  TPM_ALG_ID hashAlg,           //   IN: hash algorithm used in HMAC
  TPM2B * Z,                    //   IN: Z
  const char *label,            //   IN: a 0 terminated label using in KDF
  TPM2B * partyUInfo,           //   IN: PartyUInfo
  TPM2B * partyVInfo,           //   IN: PartyVInfo
  UINT32 sizeInBits,            //   IN: size of generated key in bit
  BYTE * keyStream              //   OUT: key buffer
  )
{
  ecprintf("%s called\n", __func__);
  return -1;
}

CRYPT_RESULT _cpri__SignEcc(
  TPM2B_ECC_PARAMETER * rOut,   //   OUT: r component of the signature
  TPM2B_ECC_PARAMETER * sOut,   //   OUT: s component of the signature
  TPM_ALG_ID scheme,            //   IN: the scheme selector
  TPM_ALG_ID hashAlg,           //   IN: the hash algorithm if need
  TPM_ECC_CURVE curveId,        //   IN: the curve used in the signature process
  TPM2B_ECC_PARAMETER * dIn,    //   IN: the private key
  TPM2B * digest,               //   IN: the digest to sign
  TPM2B_ECC_PARAMETER * kIn     //   IN: k for input
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

CRYPT_RESULT _cpri__SignRSA(
  UINT32 * sigOutSize,          //   OUT: size of signature
  BYTE * sigOut,                //   OUT: signature
  RSA_KEY * key,                //   IN: key to use
  TPM_ALG_ID scheme,            //   IN: the scheme to use
  TPM_ALG_ID hashAlg,           //   IN: hash algorithm for PKSC1v1_5
  UINT32 hInSize,               //   IN: size of digest to be signed
  BYTE * hIn                    //   IN: digest buffer
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

UINT16 _cpri__StartHMAC(
  TPM_ALG_ID hashAlg,           //   IN: the algorithm to use
  BOOL sequence,                //   IN: indicates if the state should be saved
  CPRI_HASH_STATE * state,      //   IN/OUT: the state buffer
  UINT16 keySize,               //   IN: the size of the HMAC key
  BYTE * key,                   //   IN: the HMAC key
  TPM2B * oPadKey               //   OUT: the key prepared for the oPad round
  )
{
  ecprintf("%s called\n", __func__);
  return -1;
}

BOOL _cpri__Startup(
  void)
{
  ecprintf("%s called\n", __func__);
  return 0;
}

CRYPT_RESULT _cpri__StirRandom(
  INT32 entropySize,
  BYTE * entropy)
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

CRYPT_RESULT _cpri__TestKeyRSA(
  TPM2B * d,                    //   OUT: the address to receive the
  // private exponent
  UINT32 exponent,              //   IN: the public modulu
  TPM2B * publicKey,            //   IN/OUT: an input if only one prime is
  // provided. an output if both primes are provided
  TPM2B * prime1,               //   IN: a first prime
  TPM2B * prime2                //   IN: an optional second prime
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

CRYPT_RESULT _cpri__ValidateSignatureEcc(
  TPM2B_ECC_PARAMETER * rIn,    //   IN: r component of the signature
  TPM2B_ECC_PARAMETER * sIn,    //   IN: s component of the signature
  TPM_ALG_ID scheme,            //   IN: the scheme selector
  TPM_ALG_ID hashAlg,           //   IN: the hash algorithm used (not used
  // in all schemes)
  TPM_ECC_CURVE curveId,        //   IN: the curve used in the
  // signature process
  TPMS_ECC_POINT * Qin,         //   IN: the public point of the key
  TPM2B * digest                //   IN: the digest that was signed
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

CRYPT_RESULT _cpri__ValidateSignatureRSA(
  RSA_KEY * key,                //   IN:   key to use
  TPM_ALG_ID scheme,            //   IN:   the scheme to use
  TPM_ALG_ID hashAlg,           //   IN:   hash algorithm
  UINT32 hInSize,               //   IN:   size of digest to be checked
  BYTE * hIn,                   //   IN:   digest buffer
  UINT32 sigInSize,             //   IN:   size of signature
  BYTE * sigIn,                 //   IN:   signature
  UINT16 saltSize               //   IN:   salt size for PSS
  )
{
  ecprintf("%s called\n", __func__);
  return CRYPT_FAIL;
}

int _math__Comp(
  const UINT32 aSize,           //   IN:   size of a
  const BYTE * a,               //   IN:   a buffer
  const UINT32 bSize,           //   IN:   size of b
  const BYTE * b                //   IN:   b buffer
  )
{
  ecprintf("%s called\n", __func__);
  return -1;
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

int _math__uComp(
  const UINT32 aSize,           // IN: size of a
  const BYTE * a,               // IN: a
  const UINT32 bSize,           // IN: size of b
  const BYTE * b                // IN: b
  )
{
  ecprintf("%s called\n", __func__);
  return -1;
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
