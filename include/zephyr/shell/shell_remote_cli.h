/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SHELL_SHELL_REMOTE_CLI_H_
#define ZEPHYR_INCLUDE_SHELL_SHELL_REMOTE_CLI_H_

#include <zephyr/sys/cbprintf.h>

#ifdef __cplusplus
extern "C" {
#endif

void shell_remote_cli_cbpprintf(const struct shell *sh, enum shell_vt100_color color,
                                void *package, size_t len);

#define SHELL_REMOTE_CLI_ALIGN sizeof(long long)
/**
 * @brief Print a formatted string to the remote shell
 *
 * Macro is using static packaging to build a package. Space is allocated on the stack.

 * @param _sh The shell to print to
 * @param _color The color of the text
 * @param ... The formatted string
 */
#define SHELL_REMOTE_CLI_FPRINTF(_sh, _color, ...) do { \
        int _plen; \
        void *_pkg; \
        uint32_t _options = CBPRINTF_PACKAGE_ADD_RO_STR_POS | CBPRINTF_PACKAGE_ADD_RW_STR_POS; \
	CBPRINTF_STATIC_PACKAGE(NULL, 0, _plen, SHELL_REMOTE_CLI_ALIGN, _options, __VA_ARGS__);\
        _pkg = __builtin_alloca_with_align(_plen, 8); \
	CBPRINTF_STATIC_PACKAGE(_pkg, _plen, _plen, SHELL_REMOTE_CLI_ALIGN, \
                                _options, __VA_ARGS__); \
        shell_remote_cli_cbpprintf(_sh, _color, _pkg, _plen); \
}  while (0)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SHELL_SHELL_REMOTE_CLI_H_ */
