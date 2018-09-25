/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SAMPLE_MODULE_H
#define SAMPLE_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif
#include <logging/log.h>

#define MODULE_NAME sample_module

const char *sample_module_name_get(void);
void sample_module_func(void);

#ifdef __cplusplus
}
#endif

#endif /* SAMPLE_MODULE_H */
