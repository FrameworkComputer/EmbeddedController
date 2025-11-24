/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_PANIC_UTILS_H
#define __ZEPHYR_SHIM_PANIC_UTILS_H

#include <zephyr/kernel.h>

/**
 * @brief Print the stack back trace for the specified thread
 *
 * This function does nothing if CONFIG_ARCH_STACKWALK is disabled.
 *
 * @param thread Thread id to print the stack back trace
 */
void print_stack_trace(const struct k_thread *thread);

/**
 * @brief Format the thread name for printing.  This is safe to call
 * regardless of the CONFIG_THREAD_NAME setting.
 *
 * If CONFIG_THREAD_NAME=n, this formats the thread name as a TASK_<id>.
 *
 * @param thread Kernel thread to format the name.
 * @param name Output buffer where this function writes the formatted named.
 * @param size Size of the output buffer.
 */
void get_thread_name(const struct k_thread *thread, char *name, size_t size);

/**
 * @brief Get the stack pointer for the specified thread.
 *
 * @param thread Kernel thread
 * @returns 32-bit stack pointer for the thread.
 */
uint32_t get_stack_ptr(const struct k_thread *thread);

#endif /* __ZEPHYR_SHIM_PANIC_UTILS_H */
