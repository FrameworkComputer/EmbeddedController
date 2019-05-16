/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"

#include <stdint.h>

/* Limit the size of long form encoded objects to < 64 kB. */
#define MAX_ASN1_OBJ_LEN_BYTES 3

/* Reserve space for TLV encoding */
#define SEQ_SMALL 2  /* < 128 bytes (1B type, 1B 7-bit length) */
#define SEQ_MEDIUM 3 /* < 256 bytes (1B type, 1B length size, 1B length) */
#define SEQ_LARGE 4  /* < 65536 bytes (1B type, 1B length size, 2B length) */

/* Tag related constants. */
enum {
	V_ASN1_INT = 0x02,
	V_ASN1_BIT_STRING  = 0x03,
	V_ASN1_BYTES = 0x04,
	V_ASN1_OBJ = 0x06,
	V_ASN1_UTF8 = 0x0c,
	V_ASN1_SEQUENCE    = 0x10,
	V_ASN1_SET         = 0x11,
	V_ASN1_ASCII = 0x13,
	V_ASN1_TIME = 0x18,
	V_ASN1_CONSTRUCTED = 0x20,
	/* short helpers */
	V_BITS = V_ASN1_BIT_STRING,
	V_SEQ = V_ASN1_CONSTRUCTED | V_ASN1_SEQUENCE,
	V_SET = V_ASN1_CONSTRUCTED | V_ASN1_SET,
};

struct asn1 {
	uint8_t *p;
	size_t n;
};


#define SEQ_START(X, T, L) \
	do {                     \
		int __old = (X).n;       \
		uint8_t __t = (T);       \
		int __l = (L);           \
		(X).n += __l;
#define SEQ_END(X)                                                             \
	(X).n = asn1_seq((X).p + __old, __t, __l, (X).n - __old - __l) + __old;\
	}                                                                      \
	while (0)

/* The SHA256 OID, from https://tools.ietf.org/html/rfc5754#section-3.2
 * Only the object bytes below, the DER encoding header ([0x30 0x0d])
 * is verified by the parser. */
static const uint8_t OID_SHA256_WITH_RSA_ENCRYPTION[13] = {
	0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
	0x01, 0x01, 0x0b, 0x05, 0x00
};
static const uint8_t OID_commonName[3] = {0x55, 0x04, 0x03};
static const uint8_t OID_ecdsa_with_SHA256[8] = {0x2A, 0x86, 0x48, 0xCE,
						 0x3D, 0x04, 0x03, 0x02};
static const uint8_t OID_id_ecPublicKey[7] = {0x2A, 0x86, 0x48, 0xCE, 0x3D,
					      0x02, 0x01};
static const uint8_t OID_prime256v1[8] = {0x2A, 0x86, 0x48, 0xCE,
					  0x3D, 0x03, 0x01, 0x07};
static const uint8_t OID_fido_u2f[11] = {0x2B, 0x06, 0x01, 0x04, 0x01, 0x82,
					 0xE5, 0x1C, 0x02, 0x01, 0x01};
#define OID(X) sizeof(OID_##X), OID_##X

/* ---- ASN.1 Generation ---- */

/* start a tag and return write ptr */
static uint8_t *asn1_tag(struct asn1 *ctx, uint8_t tag)
{
	ctx->p[(ctx->n)++] = tag;
	return ctx->p + ctx->n;
}

/* DER encode length and return encoded size thereof */
static int asn1_len(uint8_t *p, size_t size)
{
	if (size < 128) {
		p[0] = size;
		return 1;
	} else if (size < 256) {
		p[0] = 0x81;
		p[1] = size;
		return 2;
	} else {
		p[0] = 0x82;
		p[1] = size >> 8;
		p[2] = size;
		return 3;
	}
}

/*
 * close sequence and move encapsulated data if needed
 * return total length.
 */
static size_t asn1_seq(uint8_t *p, uint8_t tag, size_t l, size_t size)
{
	size_t tl;

	p[0] = tag;
	tl = asn1_len(p + 1, size) + 1;
	/* TODO: tl > l fail */
	if (tl < l)
		memmove(p + tl, p + l, size);

	return tl + size;
}

/* DER encode (small positive) integer */
static void asn1_int(struct asn1 *ctx, uint32_t val)
{
	uint8_t *p = asn1_tag(ctx, V_ASN1_INT);

	if (!val) {
		*p++ = 1;
		*p++ = 0;
	} else {
		int nbits = 32 - __builtin_clz(val);
		int nbytes = (nbits + 7) / 8;

		if ((nbits & 7) == 0) {
			*p++ = nbytes + 1;
			*p++ = 0;
		} else {
			*p++ = nbytes;
		}
		while (nbytes--)
			*p++ = val >> (nbytes * 8);
	}

	ctx->n = p - ctx->p;
}

/* DER encode positive p256_int */
static void asn1_p256_int(struct asn1 *ctx, const p256_int *n)
{
	uint8_t *p = asn1_tag(ctx, V_ASN1_INT);
	uint8_t bn[P256_NBYTES];
	int i;

	p256_to_bin(n, bn);
	for (i = 0; i < P256_NBYTES; ++i) {
		if (bn[i] != 0)
			break;
	}
	if (bn[i] & 0x80) {
		*p++ = P256_NBYTES - i + 1;
		*p++ = 0;
	} else {
		*p++ = P256_NBYTES - i;
	}
	for (; i < P256_NBYTES; ++i)
		*p++ = bn[i];

	ctx->n = p - ctx->p;
}

/* DER encode p256 signature */
static void asn1_sig(struct asn1 *ctx, const p256_int *r, const p256_int *s)
{
	SEQ_START(*ctx, V_SEQ, SEQ_SMALL) {
		asn1_p256_int(ctx, r);
		asn1_p256_int(ctx, s);
	}
	SEQ_END(*ctx);
}

/* DER encode printable string */
static void asn1_string(struct asn1 *ctx, uint8_t tag, const char *s)
{
	uint8_t *p = asn1_tag(ctx, tag);
	size_t n = strlen(s);

	p += asn1_len(p, n);
	while (n--)
		*p++ = *s++;

	ctx->n = p - ctx->p;
}

/* DER encode bytes */
static void asn1_object(struct asn1 *ctx, size_t n, const uint8_t *b)
{
	uint8_t *p = asn1_tag(ctx, V_ASN1_OBJ);

	p += asn1_len(p, n);
	while (n--)
		*p++ = *b++;

	ctx->n = p - ctx->p;
}

/* DER encode p256 pk */
static void asn1_pub(struct asn1 *ctx, const p256_int *x, const p256_int *y)
{
	uint8_t *p = asn1_tag(ctx, 4); /* uncompressed format */

	p256_to_bin(x, p); p += P256_NBYTES;
	p256_to_bin(y, p); p += P256_NBYTES;

	ctx->n = p - ctx->p;
}

size_t DCRYPTO_asn1_sigp(uint8_t *buf, const p256_int *r, const p256_int *s)
{
	struct asn1 asn1 = {buf, 0};

	asn1_sig(&asn1, r, s);
	return asn1.n;
}

size_t DCRYPTO_asn1_pubp(uint8_t *buf, const p256_int *x, const p256_int *y)
{
	struct asn1 asn1 = {buf, 0};

	asn1_pub(&asn1, x, y);
	return asn1.n;
}

/* ---- ASN.1 Parsing ---- */

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

/* ---- Certificate generation ---- */

static void add_common_name(struct asn1 *ctx, const char *cname)
{
	SEQ_START(*ctx, V_SEQ, SEQ_SMALL) {
		SEQ_START(*ctx, V_SET, SEQ_SMALL) {
			SEQ_START(*ctx, V_SEQ, SEQ_SMALL) {
				asn1_object(ctx, OID(commonName));
				asn1_string(ctx, V_ASN1_ASCII, cname);
			}
			SEQ_END(*ctx);
		}
		SEQ_END(*ctx);
	}
	SEQ_END(*ctx);
}

int DCRYPTO_x509_gen_u2f_cert_name(const p256_int *d, const p256_int *pk_x,
				   const p256_int *pk_y, const p256_int *serial,
				   const char *name, uint8_t *cert, const int n)
{
	struct asn1 ctx = {cert, 0};
	HASH_CTX sha;
	p256_int h, r, s;
	struct drbg_ctx drbg;

	SEQ_START(ctx, V_SEQ, SEQ_LARGE) {  /* outer seq */
	/*
	 * Grab current pointer to data to hash later.
	 * Note this will fail if cert body + cert sign is less
	 * than 256 bytes (SEQ_MEDIUM) -- not likely.
	 */
	uint8_t *body = ctx.p + ctx.n;

	/* Cert body seq */
	SEQ_START(ctx, V_SEQ, SEQ_MEDIUM) {
		/* X509 v3 */
		SEQ_START(ctx, 0xa0, SEQ_SMALL) {
			asn1_int(&ctx, 2);
		}
		SEQ_END(ctx);

		/* Serial number */
		if (serial)
			asn1_p256_int(&ctx, serial);
		else
			asn1_int(&ctx, 1);

		/* Signature algo */
		SEQ_START(ctx, V_SEQ, SEQ_SMALL) {
			asn1_object(&ctx, OID(ecdsa_with_SHA256));
		}
		SEQ_END(ctx);

		/* Issuer */
		add_common_name(&ctx, name);

		/* Expiry */
		SEQ_START(ctx, V_SEQ, SEQ_SMALL) {
			asn1_string(&ctx, V_ASN1_TIME, "20000101000000Z");
			asn1_string(&ctx, V_ASN1_TIME, "20991231235959Z");
		}
		SEQ_END(ctx);

		/* Subject */
		add_common_name(&ctx, name);

		/* Subject pk */
		SEQ_START(ctx, V_SEQ, SEQ_SMALL) {
			/* pk parameters */
			SEQ_START(ctx, V_SEQ, SEQ_SMALL) {
				asn1_object(&ctx, OID(id_ecPublicKey));
				asn1_object(&ctx, OID(prime256v1));
			}
			SEQ_END(ctx);
			/* pk bits */
			SEQ_START(ctx, V_BITS, SEQ_SMALL) {
				/* No unused bit at the end */
				asn1_tag(&ctx, 0);
				asn1_pub(&ctx, pk_x, pk_y);
			}
			SEQ_END(ctx);
		}
		SEQ_END(ctx);

		/* U2F transports indicator extension */
		SEQ_START(ctx, 0xa3, SEQ_SMALL) {
			SEQ_START(ctx, V_SEQ, SEQ_SMALL) {
			SEQ_START(ctx, V_SEQ, SEQ_SMALL) {
				asn1_object(&ctx, OID(fido_u2f));
				SEQ_START(ctx, V_ASN1_BYTES, SEQ_SMALL) {
					SEQ_START(ctx, V_BITS, SEQ_SMALL) {
						/* 3 zero bits */
						asn1_tag(&ctx, 3);
						/* usb-internal transport */
						asn1_tag(&ctx, 0x08);
					}
					SEQ_END(ctx);
				}
				SEQ_END(ctx);
			}
			SEQ_END(ctx);
			}
			SEQ_END(ctx);
		}
		SEQ_END(ctx);
	}
	SEQ_END(ctx);  /* Cert body */

	/* Sign all of cert body */
	DCRYPTO_SHA256_init(&sha, 0);
	HASH_update(&sha, body, (ctx.p + ctx.n) - body);
	p256_from_bin(HASH_final(&sha), &h);
	hmac_drbg_init_rfc6979(&drbg, d, &h);
	if (!dcrypto_p256_ecdsa_sign(&drbg, d, &h, &r, &s))
		return 0;

	/* Append X509 signature */
	SEQ_START(ctx, V_SEQ, SEQ_SMALL);
	asn1_object(&ctx, OID(ecdsa_with_SHA256));
	SEQ_END(ctx);
	SEQ_START(ctx, V_BITS, SEQ_SMALL) {
		/* no unused/zero bit at the end */
		asn1_tag(&ctx, 0);
		asn1_sig(&ctx, &r, &s);
	} SEQ_END(ctx);

	} SEQ_END(ctx); /* end of outer seq */

	return ctx.n;
}

int DCRYPTO_x509_gen_u2f_cert(const p256_int *d, const p256_int *pk_x,
			      const p256_int *pk_y, const p256_int *serial,
			      uint8_t *cert, const int n)
{
	return DCRYPTO_x509_gen_u2f_cert_name(d, pk_x, pk_y, serial,
					      serial ? STRINGIFY(BOARD) : "U2F",
					      cert, n);
}
