/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Vboot 2.1 data structures
 *
 * Offsets should be padded to 32-bit boundaries, since some architectures
 * have trouble with accessing unaligned integers.
 *
 * Note: This file is copied from
 * src/platform/vboot_reference/firmware/lib21/include/vb21_struct.h
 * and should be updated if necessary.
 */

#ifndef VBOOT_REFERENCE_VB21_STRUCT_H_
#define VBOOT_REFERENCE_VB21_STRUCT_H_
#include "2id.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Magic numbers used by vb21_struct_common.magic.
 *
 * All valid numbers should be listed here to avoid accidental overlap.
 * Numbers start at a large value, so that previous parsers (which stored
 * things like lengths and offsets at that field) will detect and reject new
 * structs as invalid.
 */
enum vb21_struct_common_magic {
	/* "Vb2B" = vb21_keyblock.c.magic */
	VB21_MAGIC_KEYBLOCK = 0x42326256,

	/* "Vb2F" = vb21_fw_preamble.c.magic */
	VB21_MAGIC_FW_PREAMBLE = 0x46326256,

	/* "Vb2I" = vb21_packed_private_key.c.magic */
	VB21_MAGIC_PACKED_PRIVATE_KEY = 0x49326256,

	/* "Vb2K" = vb2_kernel_preamble.c.magic */
	VB21_MAGIC_KERNEL_PREAMBLE = 0x4b326256,

	/* "Vb2P" = vb21_packed_key.c.magic */
	VB21_MAGIC_PACKED_KEY = 0x50326256,

	/* "Vb2S" = vb21_signature.c.magic */
	VB21_MAGIC_SIGNATURE = 0x53326256,
};

/*
 * Generic struct header for all vboot2.1 structs.  This makes it easy to
 * automatically parse and identify vboot structs (e.g., in futility).  This
 * must be the first member of the parent vboot2.1 struct.
 */
struct vb21_struct_common {
	/* Magic number; see vb21_struct_common_magic for expected values */
	uint32_t magic;

	/*
	 * Parent struct version; see each struct for the expected value.
	 *
	 * How to handle struct version mismatches, if the parser is version
	 * A.b and the data is version C.d:
	 *     1) If A.b == C.d, we're good.
	 *     2) If A != C, the data cannot be parsed at all.
	 *     3) If b < d, C.d is a newer version of data which is backwards-
	 *        compatible to old parsers.  We're good.
	 *     4) If b > d, C.d is an older version of data.  The parser should
	 *        use default values for fields added after version d.  We're
	 *        good.
	 *
	 * Struct versions start at 3.0, since the highest version of the old
	 * structures was 2.1.  This way, there is no possibility of collision
	 * for old code which depends on the version number.
	 */
	uint16_t struct_version_major;
	uint16_t struct_version_minor;

	/*
	 * Size of the parent structure and all its data, including the
	 * description and any necessary padding.  That is, all data must lie
	 * in a contiguous region of <total_size> bytes starting at the first
	 * byte of this header.
	 */
	uint32_t total_size;

	/*
	 * Size of the fixed portion of the parent structure.  If a description
	 * is present, it must start at this offset.
	 */
	uint32_t fixed_size;

	/*
	 * The object may contain an ASCII description following the fixed
	 * portion of the structure.  If it is present, it must be
	 * null-terminated, and padded with 0 (null) bytes to a multiple of 32
	 * bits.
	 *
	 * Size of ASCII description in bytes, counting null terminator and
	 * padding (if any).  Set 0 if no description is present.  If non-zero,
	 * there must be a null terminator (0) at offset (fixed_size +
	 * desc_size - 1).
	 */
	uint32_t desc_size;
} __attribute__((packed));

#define EXPECTED_VB21_STRUCT_COMMON_SIZE 20

/* Current version of vb21_packed_key struct */
#define VB21_PACKED_KEY_VERSION_MAJOR 3
#define VB21_PACKED_KEY_VERSION_MINOR 0

/*
 * Packed public key data
 *
 * The key data must be arranged like this:
 *     1) vb21_packed_key header struct h
 *     2) Key description (pointed to by h.c.fixed_size)
 *     3) Key data key (pointed to by h.key_offset)
 */
struct vb21_packed_key {
	/* Common header fields */
	struct vb21_struct_common c;

	/* Offset of key data from start of this struct */
	uint32_t key_offset;

	/* Size of key data in bytes (NOT strength of key in bits) */
	uint32_t key_size;

	/* Signature algorithm used by the key (enum vb2_signature_algorithm) */
	uint16_t sig_alg;

	/*
	 * Hash digest algorithm used with the key (enum vb2_hash_algorithm).
	 * This is explicitly specified as part of the key to prevent use of a
	 * strong key with a weak hash.
	 */
	uint16_t hash_alg;

	/* Key version */
	uint32_t key_version;

	/* Key ID */
	struct vb2_id id;
} __attribute__((packed));

#define EXPECTED_VB21_PACKED_KEY_SIZE \
	(EXPECTED_VB21_STRUCT_COMMON_SIZE + 16 + EXPECTED_ID_SIZE)

/* Current version of vb21_packed_private_key struct */
#define VB21_PACKED_PRIVATE_KEY_VERSION_MAJOR 3
#define VB21_PACKED_PRIVATE_KEY_VERSION_MINOR 0

/*
 * Packed private key data
 *
 * The key data must be arranged like this:
 *     1) vb21_packed_private_key header struct h
 *     2) Key description (pointed to by h.c.fixed_size)
 *     3) Key data key (pointed to by h.key_offset)
 */
struct vb21_packed_private_key {
	/* Common header fields */
	struct vb21_struct_common c;

	/* Offset of key data from start of this struct */
	uint32_t key_offset;

	/* Size of key data in bytes (NOT strength of key in bits) */
	uint32_t key_size;

	/* Signature algorithm used by the key (enum vb2_signature_algorithm) */
	uint16_t sig_alg;

	/*
	 * Hash digest algorithm used with the key (enum vb2_hash_algorithm).
	 * This is explicitly specified as part of the key to prevent use of a
	 * strong key with a weak hash.
	 */
	uint16_t hash_alg;

	/* Key ID */
	struct vb2_id id;
} __attribute__((packed));

#define EXPECTED_VB21_PACKED_PRIVATE_KEY_SIZE \
	(EXPECTED_VB21_STRUCT_COMMON_SIZE + 12 + EXPECTED_ID_SIZE)

/* Current version of vb21_signature struct */
#define VB21_SIGNATURE_VERSION_MAJOR 3
#define VB21_SIGNATURE_VERSION_MINOR 0

/*
 * Signature data
 *
 * The signature data must be arranged like this:
 *     1) vb21_signature header struct h
 *     2) Signature description (pointed to by h.c.fixed_size)
 *     3) Signature data (pointed to by h.sig_offset)
 */
struct vb21_signature {
	/* Common header fields */
	struct vb21_struct_common c;

	/* Offset of signature data from start of this struct */
	uint32_t sig_offset;

	/* Size of signature data in bytes */
	uint32_t sig_size;

	/* Size of the data block which was signed in bytes */
	uint32_t data_size;

	/* Signature algorithm used (enum vb2_signature_algorithm) */
	uint16_t sig_alg;

	/* Hash digest algorithm used (enum vb2_hash_algorithm) */
	uint16_t hash_alg;

	/*
	 * ID for the signature.
	 *
	 * If this is a keyblock signature entry, this is the ID of the key
	 * used to generate this signature.  This allows the firmware to
	 * quickly determine which signature block (if any) goes with the key
	 * being used by the firmware.
	 *
	 * If this is a preamble hash entry, this is the ID of the data type
	 * being hashed.  There is no key ID, because sig_alg=VB2_ALG_NONE.
	 */
	struct vb2_id id;
} __attribute__((packed));

#define EXPECTED_VB21_SIGNATURE_SIZE \
	(EXPECTED_VB21_STRUCT_COMMON_SIZE + 16 + EXPECTED_ID_SIZE)

/* Current version of vb21_keyblock struct */
#define VB21_KEYBLOCK_VERSION_MAJOR 3
#define VB21_KEYBLOCK_VERSION_MINOR 0

/*
 * Key block.  This contains a signed, versioned key for use in the next stage
 * of verified boot.
 *
 * The key block data must be arranged like this:
 *     1) vb21_keyblock header struct h
 *     2) Keyblock description (pointed to by h.c.fixed_size)
 *     3) Data key (pointed to by h.data_key_offset)
 *     4) Signatures (first signature pointed to by h.sig_offset)
 *
 * The signatures from 4) must cover all the data from 1), 2), 3).  That is,
 * signatures must sign all data up to sig_offset.
 */
struct vb21_keyblock {
	/* Common header fields */
	struct vb21_struct_common c;

	/* Flags (VB2_KEY_BLOCK_FLAG_*) */
	uint32_t flags;

	/*
	 * Offset of key (struct vb21_packed_key) to use in next stage of
	 * verification, from start of the keyblock.
	 */
	uint32_t key_offset;

	/* Number of keyblock signatures which follow */
	uint32_t sig_count;

	/*
	 * Offset of the first signature (struct vb21_signature) from the start
	 * of the keyblock.
	 *
	 * Signatures sign the contents of this struct and the data pointed to
	 * by data_key_offset, but not themselves or other signatures.
	 *
	 * For the firmware, there may be only one signature.
	 *
	 * Kernels often have at least two signatures - one using the kernel
	 * subkey from the RW firmware (for signed kernels) and one which is
	 * simply a SHA-512 hash (for unsigned developer kernels).
	 *
	 * The ID for each signature indicates which key was used to generate
	 * the signature.
	 */
	uint32_t sig_offset;
} __attribute__((packed));

#define EXPECTED_VB21_KEYBLOCK_SIZE (EXPECTED_VB21_STRUCT_COMMON_SIZE + 16)

/* Current version of vb21_fw_preamble struct */
#define VB21_FW_PREAMBLE_VERSION_MAJOR 3
#define VB21_FW_PREAMBLE_VERSION_MINOR 0

/* Flags for vb21_fw_preamble.flags */
/* Reserved; do not use */
#define VB21_FIRMWARE_PREAMBLE_RESERVED0 0x00000001
/* Do not allow use of any hardware crypto accelerators. */
#define VB21_FIRMWARE_PREAMBLE_DISALLOW_HWCRYPTO 0x00000002

/*
 * Firmware preamble
 *
 * The preamble data must be arranged like this:
 *     1) vb21_fw_preamble header struct h
 *     2) Preamble description (pointed to by h.c.fixed_size)
 *     3) Hashes (pointed to by h.hash_offset)
 *     4) Signature (pointed to by h.sig_offset)
 *
 * The signature 4) must cover all the data from 1), 2), 3).
 */
struct vb21_fw_preamble {
	/* Common header fields */
	struct vb21_struct_common c;

	/* Flags; see VB21_FIRMWARE_PREAMBLE_* */
	uint32_t flags;

	/* Firmware version */
	uint32_t fw_version;

	/* Offset of signature (struct vb21_signature) for this preamble */
	uint32_t sig_offset;

	/*
	 * The preamble contains a list of hashes (struct vb21_signature) for
	 * the various firmware components.  These have sig_alg=VB2_SIG_NONE,
	 * and the ID for each hash identifies the component being hashed.
	 * The calling firmware is responsible for knowing where to find those
	 * components, which may be on a different storage device than this
	 * preamble.
	 */

	/* Number of hash entries */
	uint32_t hash_count;

	/* Offset of first hash entry from start of preamble */
	uint32_t hash_offset;
} __attribute__((packed));

#define EXPECTED_VB21_FW_PREAMBLE_SIZE (EXPECTED_VB21_STRUCT_COMMON_SIZE + 20)

#ifdef __cplusplus
}
#endif

#endif /* VBOOT_REFERENCE_VB21_STRUCT_H_ */
