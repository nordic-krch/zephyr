/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Kernel initialization module
 *
 * This module contains routines that are used to initialize the kernel.
 */
#include <autoconf.h>
#include <string.h>
#include <linker/linker-defs.h>
#include <kernel.h>
K_STACK_DEFINE(z_main_stack, CONFIG_MAIN_STACK_SIZE);

/* LCOV_EXCL_START
 *
 * This code is called so early in the boot process that code coverage
 * doesn't work properly. In addition, not all arches call this code,
 * some like x86 do this with optimized assembly
 */

/**
 *
 * @brief Clear BSS
 *
 * This routine clears the BSS region, so all bytes are 0.
 *
 * @return N/A
 */
void z_bss_zero(void)
{
	(void)memset(__bss_start, 0, __bss_end - __bss_start);
#if 0
#ifdef DT_CCM_BASE_ADDRESS
	(void)memset(&__ccm_bss_start, 0,
		     ((u32_t) &__ccm_bss_end - (u32_t) &__ccm_bss_start));
#endif
#ifdef DT_DTCM_BASE_ADDRESS
	(void)memset(&__dtcm_bss_start, 0,
		     ((u32_t) &__dtcm_bss_end - (u32_t) &__dtcm_bss_start));
#endif
#ifdef CONFIG_CODE_DATA_RELOCATION
	extern void bss_zeroing_relocation(void);

	bss_zeroing_relocation();
#endif	/* CONFIG_CODE_DATA_RELOCATION */
#ifdef CONFIG_COVERAGE_GCOV
	(void)memset(&__gcov_bss_start, 0,
		 ((u32_t) &__gcov_bss_end - (u32_t) &__gcov_bss_start));
#endif
#endif
}

#ifdef CONFIG_STACK_CANARIES
extern volatile uintptr_t __stack_chk_guard;
#endif /* CONFIG_STACK_CANARIES */


#ifdef CONFIG_XIP
/**
 *
 * @brief Copy the data section from ROM to RAM
 *
 * This routine copies the data section from ROM to RAM.
 *
 * @return N/A
 */
void z_data_copy(void)
{
	(void)memcpy(&__data_ram_start, &__data_rom_start,
		 __data_ram_end - __data_ram_start);
#ifdef CONFIG_ARCH_HAS_RAMFUNC_SUPPORT
	(void)memcpy(&_ramfunc_ram_start, &_ramfunc_rom_start,
		 (uintptr_t) &_ramfunc_ram_size);
#endif /* CONFIG_ARCH_HAS_RAMFUNC_SUPPORT */
#ifdef DT_CCM_BASE_ADDRESS
	(void)memcpy(&__ccm_data_start, &__ccm_data_rom_start,
		 __ccm_data_end - __ccm_data_start);
#endif
#ifdef DT_DTCM_BASE_ADDRESS
	(void)memcpy(&__dtcm_data_start, &__dtcm_data_rom_start,
		 __dtcm_data_end - __dtcm_data_start);
#endif
#ifdef CONFIG_CODE_DATA_RELOCATION
	extern void data_copy_xip_relocation(void);

	data_copy_xip_relocation();
#endif	/* CONFIG_CODE_DATA_RELOCATION */
#ifdef CONFIG_USERSPACE
#ifdef CONFIG_STACK_CANARIES
	/* stack canary checking is active for all C functions.
	 * __stack_chk_guard is some uninitialized value living in the
	 * app shared memory sections. Preserve it, and don't make any
	 * function calls to perform the memory copy. The true canary
	 * value gets set later in z_cstart().
	 */
	uintptr_t guard_copy = __stack_chk_guard;
	u8_t *src = (u8_t *)&_app_smem_rom_start;
	u8_t *dst = (u8_t *)&_app_smem_start;
	u32_t count = _app_smem_end - _app_smem_start;

	guard_copy = __stack_chk_guard;
	while (count > 0) {
		*(dst++) = *(src++);
		count--;
	}
	__stack_chk_guard = guard_copy;
#else
	(void)memcpy(&_app_smem_start, &_app_smem_rom_start,
		 _app_smem_end - _app_smem_start);
#endif /* CONFIG_STACK_CANARIES */
#endif /* CONFIG_USERSPACE */
}
#endif /* CONFIG_XIP */

/* LCOV_EXCL_STOP */


/* LCOV_EXCL_START */

void __weak main(void)
{
	/* NOP default main() if the application does not provide one. */
	/*arch_nop();*/
}

/* defined(CONFIG_ENTROPY_HAS_DRIVER) || defined(CONFIG_TEST_RANDOM_GENERATOR) */

/**
 *
 * @brief Initialize kernel
 *
 * This routine is invoked when the system is ready to run C code. The
 * processor must be running in 32-bit mode, and the BSS must have been
 * cleared/zeroed.
 *
 * @return Does not return
 */
#include <cmsis_compiler.h>
FUNC_NORETURN void z_cstart(void)
{
	//__enable_irq();
	main();

	CODE_UNREACHABLE; /* LCOV_EXCL_LINE */
}
