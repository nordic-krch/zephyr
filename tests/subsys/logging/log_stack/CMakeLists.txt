# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(log_stack)

target_sources_ifdef(CONFIG_CPLUSPLUS app PRIVATE src/maincxx.cxx)

target_sources_ifndef(CONFIG_CPLUSPLUS app PRIVATE src/main.c)
