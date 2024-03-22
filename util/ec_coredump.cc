/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * ec_coredump utility for extracting a coredump from the EC and storing it in
 * the Zephyr coredump format. If the EC does not support coredump, this utility
 * will fail and not create any files. On success, two files will be created:
 * `coredump` and `panicinfo`. The panicinfo file is useful for coupling with
 * a panicinfo captured by the crash collector.
 */
#include "comm-host.h"
#include "misc_util.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <chromeos/ec/panic_defs.h>
#include <fstream>
#include <iostream>
#include <vector>

#define CROS_EC_DEV_NAME "cros_ec"

bool verbose = false;

struct mem_segment {
	uint32_t addr_start;
	uint32_t addr_end;
	uint32_t size;
	uint8_t *mem;
	struct mem_segment *next;
};

#define COREDUMP_HDR_VER 1

#define COREDUMP_ARCH_HDR_ID 'A'

#define COREDUMP_MEM_HDR_ID 'M'
#define COREDUMP_MEM_HDR_VER 1

#define COREDUMP_BASENAME "coredump"
#define PANICINFO_BASENAME "panicinfo"

/* Target code */
enum coredump_tgt_code {
	COREDUMP_TGT_UNKNOWN = 0,
	COREDUMP_TGT_X86,
	COREDUMP_TGT_X86_64,
	COREDUMP_TGT_ARM_CORTEX_M,
	COREDUMP_TGT_RISC_V,
	COREDUMP_TGT_XTENSA,
	COREDUMP_TGT_NDS32,
};

struct arm_arch_block {
	struct {
		uint32_t r0;
		uint32_t r1;
		uint32_t r2;
		uint32_t r3;
		uint32_t r12;
		uint32_t lr;
		uint32_t pc;
		uint32_t xpsr;
		uint32_t sp;

		/* callee registers - optionally collected in V2 */
		uint32_t r4;
		uint32_t r5;
		uint32_t r6;
		uint32_t r7;
		uint32_t r8;
		uint32_t r9;
		uint32_t r10;
		uint32_t r11;
	} r;
} __packed;

/* Coredump header */
struct coredump_hdr_t {
	/* 'Z', 'E' */
	char id[2];

	/* Header version */
	uint16_t hdr_version;

	/* Target code */
	uint16_t tgt_code;

	/* Pointer size in Log2 */
	uint8_t ptr_size_bits;

	uint8_t flag;

	/* Coredump Reason given */
	unsigned int reason;
} __packed;

/* Architecture-specific block header */
struct coredump_arch_hdr_t {
	/* COREDUMP_ARCH_HDR_ID */
	char id;

	/* Header version */
	uint16_t hdr_version;

	/* Number of bytes in this block (excluding header) */
	uint16_t num_bytes;
} __packed;

/* Memory block header */
struct coredump_mem32_hdr_t {
	/* COREDUMP_MEM_HDR_ID */
	char id;

	/* Header version */
	uint16_t hdr_version;

	/* Address of start of memory region */
	uint32_t start;

	/* Address of end of memory region */
	uint32_t end;
} __packed;

struct coredump_mem64_hdr_t {
	/* COREDUMP_MEM_HDR_ID */
	char id;

	/* Header version */
	uint16_t hdr_version;

	/* Address of start of memory region */
	uint64_t start;

	/* Address of end of memory region */
	uint64_t end;
} __packed;

static int write_zephyr_coredump_memory_block(struct mem_segment &segment,
					      std::ofstream &output_file)
{
	struct coredump_mem32_hdr_t hdr = {
		.id = COREDUMP_MEM_HDR_ID,
		.hdr_version = COREDUMP_MEM_HDR_VER,
		.start = segment.addr_start,
		.end = segment.addr_end,
	};

	if (verbose) {
		std::cout << "Writing Zephyr coredump memory block..."
			  << std::endl;
		std::cout << "\tStart: " << std::hex << hdr.start << std::endl
			  << "\tEnd: " << std::hex << hdr.end << std::endl;
	}

	output_file.write((char *)&hdr, sizeof(hdr));
	output_file.write((char *)segment.mem, segment.size);

	return 0;
}

static int write_zephyr_coredump_cortex_arch_info(struct panic_data &pdata,
						  std::ofstream &output_file)
{
	struct arm_arch_block arch_blk;

	struct coredump_arch_hdr_t hdr = {
		.id = COREDUMP_ARCH_HDR_ID,
		.hdr_version = 2,
		.num_bytes = sizeof(arch_blk),
	};

	if (verbose)
		std::cout << "Writing " << hdr.num_bytes
			  << " byte arch info header..." << std::endl;

	(void)memset(&arch_blk, 0, sizeof(arch_blk));

	arch_blk.r.r0 = pdata.cm.frame[CORTEX_PANIC_FRAME_REGISTER_R0];
	arch_blk.r.r1 = pdata.cm.frame[CORTEX_PANIC_FRAME_REGISTER_R1];
	arch_blk.r.r2 = pdata.cm.frame[CORTEX_PANIC_FRAME_REGISTER_R2];
	arch_blk.r.r3 = pdata.cm.frame[CORTEX_PANIC_FRAME_REGISTER_R3];
	arch_blk.r.r12 = pdata.cm.frame[CORTEX_PANIC_FRAME_REGISTER_R12];
	arch_blk.r.lr = pdata.cm.frame[CORTEX_PANIC_FRAME_REGISTER_LR];
	arch_blk.r.pc = pdata.cm.frame[CORTEX_PANIC_FRAME_REGISTER_PC];
	arch_blk.r.xpsr = pdata.cm.frame[CORTEX_PANIC_FRAME_REGISTER_PSR];

	arch_blk.r.sp = pdata.cm.regs[CORTEX_PANIC_REGISTER_PSP];
	arch_blk.r.r4 = pdata.cm.regs[CORTEX_PANIC_REGISTER_R4];
	arch_blk.r.r5 = pdata.cm.regs[CORTEX_PANIC_REGISTER_R5];
	arch_blk.r.r6 = pdata.cm.regs[CORTEX_PANIC_REGISTER_R6];
	arch_blk.r.r7 = pdata.cm.regs[CORTEX_PANIC_REGISTER_R7];
	arch_blk.r.r8 = pdata.cm.regs[CORTEX_PANIC_REGISTER_R8];
	arch_blk.r.r9 = pdata.cm.regs[CORTEX_PANIC_REGISTER_R9];
	arch_blk.r.r10 = pdata.cm.regs[CORTEX_PANIC_REGISTER_R10];
	arch_blk.r.r11 = pdata.cm.regs[CORTEX_PANIC_REGISTER_R11];

	output_file.write((char *)&hdr, sizeof(hdr));
	output_file.write((char *)&arch_blk, sizeof(arch_blk));

	return 0;
}

static int write_zephyr_coredump_header(struct panic_data &pdata,
					std::ofstream &output_file)
{
	struct coredump_hdr_t hdr = {
		.id = { 'Z', 'E' },
		.hdr_version = COREDUMP_HDR_VER,
	};

	if (verbose)
		std::cout << "Writing Zephyr coredump header..." << std::endl;

	switch (pdata.arch) {
	case PANIC_ARCH_CORTEX_M:
		hdr.tgt_code = COREDUMP_TGT_ARM_CORTEX_M;
		hdr.ptr_size_bits = 5; /* 2^5 = 32 */
		hdr.reason = pdata.cm.regs[CORTEX_PANIC_REGISTER_R4];
		break;
	case PANIC_ARCH_NDS32_N8:
		hdr.tgt_code = COREDUMP_TGT_NDS32;
		hdr.ptr_size_bits = 5; /* 2^5 = 32 */
		break;
	case PANIC_ARCH_RISCV_RV32I:
		hdr.tgt_code = COREDUMP_TGT_RISC_V;
		hdr.ptr_size_bits = 5; /* 2^5 = 32 */
		hdr.reason = pdata.cm.regs[11];
		break;
	default:
		std::cerr << "Error: Unknown architecture" << std::endl;
		return -1;
	}

	output_file.write((char *)&hdr, sizeof(hdr));

	return 0;
}

static int get_panic_info(struct panic_data &pdata)
{
	int bytes_read;
	struct ec_params_get_panic_info_v1 params = {
		.preserve_old_hostcmd_flag = 1,
	};

	if (verbose)
		std::cout << "Getting panic info..." << std::endl;

	bytes_read = ec_command(EC_CMD_GET_PANIC_INFO, 1, &params,
				sizeof(params), ec_inbuf, ec_max_insize);
	if (bytes_read < 0) {
		std::cerr << "Error: Failed to get panic info" << std::endl;
		return -1;
	}

	if (bytes_read == 0) {
		std::cerr << "Error: Panic info is empty" << std::endl;
		return -1;
	}

	if (bytes_read > sizeof(pdata)) {
		std::cerr << "Error: Panic info larger than expected"
			  << std::endl;
		return -1;
	}

	memcpy(&pdata, ec_inbuf, bytes_read);

	if (pdata.struct_version > 2 || pdata.struct_version == 0) {
		std::cerr << "Error: Unexpected struct version: "
			  << pdata.struct_version << std::endl;
		return -1;
	}

	if (pdata.struct_size != bytes_read) {
		std::cerr
			<< "Error: Panic info struct_size does not match bytes read"
			<< std::endl;
		return -1;
	}

	if (pdata.reserved != 0) {
		std::cerr << "Error: Unexpected panic reserved value "
			  << pdata.reserved << std::endl;
		return -1;
	}

	return 0;
}

static int write_panic_info(struct panic_data &pdata,
			    std::ofstream &output_file)
{
	if (verbose)
		std::cout << "Writing " << std::dec << pdata.struct_size
			  << " bytes of panic info..." << std::endl;

	output_file.write((char *)&pdata, sizeof(pdata));

	return 0;
}

static void print_segment(struct mem_segment *segment)
{
	std::cout << "\tStart: " << std::hex << segment->addr_start << std::endl
		  << "\tEnd: " << std::hex << segment->addr_end << std::endl
		  << "\tSize: " << std::hex << segment->size << std::endl;
}

static void print_segments(struct mem_segment *root)
{
	struct mem_segment *current = root;
	int i = 0;

	while (current) {
		std::cout << "Segment " << i++ << ":" << std::endl;
		print_segment(current);
		current = current->next;
	}
}

static struct mem_segment *get_segment(uint16_t index)
{
	int rv;
	uint32_t offset = 0;
	struct mem_segment *segment = nullptr;
	struct ec_response_memory_dump_get_entry_info entry_info_response;
	struct ec_params_memory_dump_get_entry_info entry_info_params = {
		.memory_dump_entry_index = index,
	};

	if (verbose)
		std::cout << "Fetching memory dump entry " << index << "..."
			  << std::endl;

	rv = ec_command(EC_CMD_MEMORY_DUMP_GET_ENTRY_INFO, 0,
			&entry_info_params, sizeof(entry_info_params),
			&entry_info_response, sizeof(entry_info_response));
	if (rv < 0) {
		std::cerr << "Error: Failed to get memory dump info for entry "
			  << std::endl;
		goto cleanup;
	}

	if (verbose)
		std::cout << "\tStart address: " << std::hex
			  << entry_info_response.address << std::endl
			  << "\tSize: " << std::hex << entry_info_response.size
			  << std::endl;

	segment = (struct mem_segment *)malloc(sizeof(struct mem_segment));
	if (segment == NULL) {
		std::cerr << "Error: malloc failed\n";
		goto cleanup;
	}
	segment->addr_start = entry_info_response.address;
	segment->addr_end =
		entry_info_response.address + entry_info_response.size;
	segment->size = entry_info_response.size;
	segment->mem = (uint8_t *)malloc(segment->size);
	if (segment->mem == NULL) {
		std::cerr << "Error: malloc failed\n";
		goto cleanup;
	}

	/* Keep fetching until entire segment is copied */
	while (offset < segment->size) {
		struct ec_params_memory_dump_read_memory read_mem_params = {
			.memory_dump_entry_index = index,
			.address = segment->addr_start + offset,
			.size = segment->size - offset,
		};

		rv = ec_command(EC_CMD_MEMORY_DUMP_READ_MEMORY, 0,
				&read_mem_params, sizeof(read_mem_params),
				ec_inbuf, ec_max_insize);
		if (rv <= 0) {
			std::cerr << "Error: Failed to read EC memory at "
				  << std::hex << read_mem_params.address
				  << std::endl;
			goto cleanup;
		}

		memcpy(segment->mem + offset, ec_inbuf, rv);

		offset += rv;
	};
	return segment;
cleanup:
	if (segment) {
		free(segment->mem);
		free(segment);
	}
	return nullptr;
}

/* Insert segment into linked list sorted by start address */
static struct mem_segment *insert_segment_sorted(struct mem_segment *root,
						 struct mem_segment *segment)
{
	struct mem_segment *current = root;
	struct mem_segment *prev = nullptr;

	if (verbose)
		std::cout << "Inserting segment into sorted list..."
			  << std::endl;

	while (current && current->addr_start < segment->addr_start) {
		prev = current;
		current = current->next;
	}

	if (prev == nullptr) {
		/* Insert segment at the beginning of the list. */
		segment->next = root;
		root = segment;
	} else {
		/* Insert segment before the current segment. */
		segment->next = current;
		prev->next = segment;
	}

	return root;
}

/* Merge memory segments that are overlapping or touching.
 * Assumes list is already sorted by starting address
 */
int merge_segments(struct mem_segment *root)
{
	struct mem_segment *current = root;
	int i = 0;
	if (verbose)
		std::cout << "Merging segments..." << std::endl;

	while (current && current->next) {
		struct mem_segment *next = current->next;
		if (current->addr_end < next->addr_start) {
			// No overlap
			current = next;
			i++;
			continue;
		}
		int32_t overlap = current->addr_end - next->addr_start;
		int32_t new_size = current->size + next->size - overlap;
		if (verbose) {
			std::cout << "Merging segment " << i++ << " and " << i++
				  << ", with " << overlap << " byte overlap..."
				  << std::endl;
		}
		current->mem = (uint8_t *)realloc(current->mem, new_size);
		if (current->mem == NULL) {
			std::cerr << "Error: realloc failed" << std::endl;
			return -1;
		}
		memcpy(current->mem + current->size, next->mem + overlap,
		       next->size - overlap);
		current->size = new_size;
		current->addr_end = next->addr_end;
		current->next = next->next;
		free(next->mem);
		free(next);

		current = current->next;
	}

	return 0;
}

struct mem_segment *get_segments(void)
{
	int rv;
	/* Simple local structs for storing a memory dump */
	uint16_t entry_count;
	struct mem_segment *root = nullptr;
	struct ec_response_memory_dump_get_metadata metadata_response;

	/* Fetch memory dump metadata */
	if (verbose)
		std::cout << "Getting memory dump metadata..." << std::endl;
	rv = ec_command(EC_CMD_MEMORY_DUMP_GET_METADATA, 0, NULL, 0,
			&metadata_response, sizeof(metadata_response));
	if (rv < 0) {
		std::cerr << "Error: Failed to get memory dump metadata from EC"
			  << std::endl;
		goto cleanup;
	}
	entry_count = metadata_response.memory_dump_entry_count;
	if (entry_count == 0) {
		std::cerr << "Error: EC memory dump is empty" << std::endl;
		goto cleanup;
	}
	if (verbose)
		std::cout << "Fetching " << entry_count
			  << " memory dump entries..." << std::endl;

	/* Fetch all memory segments */
	for (uint16_t index = 0; index < entry_count; index++) {
		struct mem_segment *segment = get_segment(index);
		if (segment == NULL) {
			std::cerr << "Error: Failed to get segment " << index
				  << std::endl;
			goto cleanup;
		}
		/* Insert segment into sorted linked list */
		root = insert_segment_sorted(root, segment);
	}

	if (verbose)
		print_segments(root);

	if (merge_segments(root) != 0) {
		std::cerr << "Failed to merge segments" << std::endl;
		goto cleanup;
	}

	if (verbose)
		print_segments(root);

	return root;
cleanup:
	struct mem_segment *current = root;
	while (current) {
		struct mem_segment *next = current->next;
		free(current->mem);
		free(current);
		current = next;
	}
	return nullptr;
}

void print_help()
{
	std::cout << "Usage: ec_coredump [OPTIONS] OUTPUT_PATH\n"
		  << "Options:\n"
		  << "  -v, --verbose\tDisplay verbose output\n"
		  << "  -h, --help\tShow this help message and exit\n";
}

int main(int argc, char *argv[])
{
	int rv;
	std::vector<std::string> args(argv + 1, argv + argc);
	std::string output_path;
	struct panic_data pdata;

	for (size_t i = 0; i < args.size(); ++i) {
		if (args[i] == "-v" || args[i] == "--verbose") {
			verbose = true;
		} else if (args[i] == "-h" || args[i] == "--help") {
			print_help();
			return 0;
		} else if (output_path.empty()) {
			output_path = args[i];
		} else {
			std::cerr << "Error: Invalid argument '" << args[i]
				  << "'.\n";
			return -1;
		}
	}

	if (comm_init_dev(CROS_EC_DEV_NAME)) {
		std::cerr << "Error: Failed to initialize " << CROS_EC_DEV_NAME
			  << std::endl;
		return -1;
	}

	if (comm_init_buffer()) {
		std::cerr << "Error: Failed to initialize buffers\n";
		return -1;
	}

	if (output_path.empty()) {
		std::cerr << "Error: No coredump output path provided"
			  << std::endl;
		return -1;
	}

	rv = get_panic_info(pdata);
	if (rv != 0) {
		std::cerr << "Error: Failed to get panic info" << std::endl;
		return -1;
	}

	std::string panic_output_filename(output_path + "/" +
					  PANICINFO_BASENAME);
	if (verbose)
		std::cout << "Opening panicinfo output file '"
			  << panic_output_filename << "'..." << std::endl;
	std::ofstream panic_output_file(panic_output_filename);
	if (!panic_output_file) {
		std::cerr << "Error: Unable to open panic output file '"
			  << panic_output_filename << "'" << std::endl;
		return -1;
	}

	rv = write_panic_info(pdata, panic_output_file);
	panic_output_file.close();
	if (rv != 0) {
		std::cerr << "Error: Failed to write panic info to "
			  << panic_output_filename << std::endl;
		return -1;
	}

	std::string coredump_output_filename(output_path + "/" +
					     COREDUMP_BASENAME);
	if (verbose)
		std::cout << "Opening coredump output file '"
			  << coredump_output_filename << "'..." << std::endl;
	std::ofstream coredump_output_file(coredump_output_filename);
	if (!coredump_output_file) {
		std::cerr << "Error: Unable to open coredump output file '"
			  << coredump_output_filename << "'" << std::endl;
		return -1;
	}

	rv = write_zephyr_coredump_header(pdata, coredump_output_file);
	if (rv != 0) {
		std::cerr << "Error: Failed to write zephyr coredump header"
			  << std::endl;
		coredump_output_file.close();
		return -1;
	}

	switch (pdata.arch) {
	case PANIC_ARCH_CORTEX_M:
		rv = write_zephyr_coredump_cortex_arch_info(
			pdata, coredump_output_file);
		break;
	default:
		std::cerr << "Error: Unhandled architecture" << std::endl;
		rv = -1;
	}
	if (rv != 0) {
		std::cerr << "Error: Failed to write zephyr coredump arch info"
			  << std::endl;
		coredump_output_file.close();
		return -1;
	}

	struct mem_segment *segments = get_segments();
	if (segments == nullptr) {
		std::cerr << "Error: Failed to get segments" << std::endl;
		coredump_output_file.close();
		return -1;
	}
	while (segments != nullptr) {
		struct mem_segment *next = segments->next;
		/* Loop will continue after error so segments are free'd */
		if (rv == 0) {
			rv = write_zephyr_coredump_memory_block(
				*segments, coredump_output_file);
			if (rv != 0)
				std::cerr
					<< "Error: Failed to write zephyr coredump memory block"
					<< std::endl;
		}
		free(segments->mem);
		free(segments);
		segments = next;
	}
	coredump_output_file.close();

	return rv;
}