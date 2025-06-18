# Copyright (c) 2024 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

ExternalZephyrProject_Add(
  APPLICATION flpr
  SOURCE_DIR ${APP_DIR}/flpr
  BOARD nrf54h20dk/nrf54h20/cpuflpr
)

add_dependencies(${DEFAULT_IMAGE} flpr)
sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} flpr)
