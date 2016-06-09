/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"

#include <stdint.h>

/* Limit the size of long form encoded objects to < 64 kB. */
#define MAX_ASN1_OBJ_LEN_BYTES 3

/* Tag related constants. */
#define V_ASN1_CONSTRUCTED 0x20
#define V_ASN1_SEQUENCE    0x10
#define V_ASN1_BIT_STRING  0x03

/* The SHA256 OID, from https://tools.ietf.org/html/rfc5754#section-3.2
 * Only the object bytes below, the DER encoding header ([0x30 0x0d])
 * is verified by the parser. */
static const uint8_t OID_SHA256_WITH_RSA_ENCRYPTION[13] = {
	0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
	0x01, 0x01, 0x0b, 0x05, 0x00
};


/*
 * An ASN.1 DER (Definite Encoding Rules) parser.
 * Details about the format are available here:
 *     https://en.wikipedia.org/wiki/X.690#Definite_form
 */
static size_t asn1_parse(const uint8_t **p, size_t available,
			uint8_t expected_type, const uint8_t **out,
			size_t *out_len, size_t *remaining)
{
	const size_t tag_len = 1;
	const uint8_t *in = *p;
	size_t obj_len = 0;
	size_t obj_len_bytes;
	size_t consumed;

	if (available < 2)
		return 0;
	if (in[0] != expected_type)  /* in[0] specifies the tag. */
		return 0;

	if ((in[1] & 128) == 0) {
		/* Short-length encoding (i.e. obj_len <= 127). */
		obj_len = in[1];
		obj_len_bytes = 1;
	} else {
		int i;

		obj_len_bytes = 1 + (in[1] & 127);
		if (obj_len_bytes > MAX_ASN1_OBJ_LEN_BYTES ||
			tag_len + obj_len_bytes > available)
			return 0;

		if (in[2] == 0)
			/* Definite form encoding requires minimal
			 * length encoding. */
			return 0;
		for (i = 0; i < obj_len_bytes - 1; i++) {
			obj_len <<= 8;
			obj_len |= in[tag_len + 1 + i];
		}
	}

	consumed = tag_len + obj_len_bytes + obj_len;
	if (consumed > available)
		return 0;    /* Invalid object length.*/
	if (out)
		*out = &in[tag_len + obj_len_bytes];
	if (out_len)
		*out_len = obj_len;

	*p = in + consumed;
	if (remaining)
		*remaining = available - consumed;
	return consumed;
}

static size_t asn1_parse_certificate(const uint8_t **p, size_t *available)
{
	size_t consumed;
	size_t obj_len;
	const uint8_t *in = *p;

	consumed = asn1_parse(&in, *available,
			V_ASN1_CONSTRUCTED | V_ASN1_SEQUENCE,
			NULL, &obj_len, NULL);
	if (consumed == 0 || consumed != *available)  /* Invalid SEQUENCE. */
		return 0;
	*p += consumed - obj_len;
	*available -= consumed - obj_len;
	return 1;
}

static size_t asn1_parse_tbs(const uint8_t **p, size_t *available,
			size_t *tbs_len)
{
	size_t consumed;

	consumed = asn1_parse(p, *available,
			V_ASN1_CONSTRUCTED | V_ASN1_SEQUENCE,
			NULL, NULL, available);
	if (consumed == 0)
		return 0;
	*tbs_len = consumed;
	return 1;
}

static size_t asn1_parse_signature_algorithm(const uint8_t **p,
					size_t *available)
{
	const uint8_t *alg_oid;
	size_t alg_oid_len;

	if (!asn1_parse(p, *available, V_ASN1_CONSTRUCTED | V_ASN1_SEQUENCE,
				&alg_oid, &alg_oid_len, available))
		return 0;
	if (alg_oid_len != sizeof(OID_SHA256_WITH_RSA_ENCRYPTION))
		return 0;
	if (memcmp(alg_oid, OID_SHA256_WITH_RSA_ENCRYPTION,
			sizeof(OID_SHA256_WITH_RSA_ENCRYPTION)) != 0)
		return 0;
	return 1;
}

static size_t asn1_parse_signature_value(const uint8_t **p, size_t *available,
					const uint8_t **sig, size_t *sig_len)
{
	if (!asn1_parse(p, *available, V_ASN1_BIT_STRING,
				sig, sig_len, available))
		return 0;
	if (*available != 0)
		return 0;     /* Not all input bytes consumed. */
	return 1;
}

/* This method verifies that the provided X509 certificate was issued
 * by the specified certifcate authority.
 *
 * cert is a pointer to a DER encoded X509 certificate, as specified
 * in https://tools.ietf.org/html/rfc5280#section-4.1.  In ASN.1
 * notation, the certificate has the following structure:
 *
 *   Certificate  ::=  SEQUENCE  {
 *        tbsCertificate       TBSCertificate,
 *        signatureAlgorithm   AlgorithmIdentifier,
 *        signatureValue       BIT STRING  }
 *
 *   TBSCertificate  ::=  SEQUENCE  { }
 *   AlgorithmIdentifier  ::=  SEQUENCE  { }
 *
 * where signatureValue = SIGN(HASH(tbsCertificate)), with SIGN and
 * HASH specified by signatureAlgorithm.
 */
int DCRYPTO_x509_verify(const uint8_t *cert, size_t len,
			const struct RSA *ca_pub_key)
{
	const uint8_t *p = cert;
	const uint8_t *tbs;
	size_t tbs_len;
	const uint8_t *sig;
	size_t sig_len;

	uint8_t digest[SHA256_DIGEST_SIZE];

	/* Read Certificate SEQUENCE. */
	if (!asn1_parse_certificate(&p, &len))
		return 0;

	/* Read tbsCertificate SEQUENCE. */
	tbs = p;
	if (!asn1_parse_tbs(&p, &len, &tbs_len))
		return 0;

	/* Read signatureAlgorithm SEQUENCE. */
	if (!asn1_parse_signature_algorithm(&p, &len))
		return 0;

	/* Read signatureValue BIT STRING. */
	if (!asn1_parse_signature_value(&p, &len, &sig, &sig_len))
		return 0;

	/* Check that the signature length corresponds to the issuer's
	 * public key size. */
	if (sig_len != bn_size(&ca_pub_key->N) &&
		sig_len != bn_size(&ca_pub_key->N) + 1)
		return 0;
	/* Check that leading signature bytes (if any) are zero. */
	if (sig_len == bn_size(&ca_pub_key->N) + 1) {
		if (sig[0] != 0)
			return 0;
		sig++;
		sig_len--;
	}

	DCRYPTO_SHA256_hash(tbs, tbs_len, digest);
	return DCRYPTO_rsa_verify(ca_pub_key, digest, sizeof(digest),
				sig, sig_len, PADDING_MODE_PKCS1, HASH_SHA256);
}
