/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/shell/shell_remote.h>

#ifdef CONFIG_SOC_NRF54H20_CPUAPP
#if defined(CONFIG_SHELL_REMOTE_PPR) && DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(cpuapp_cpuppr_ipc))
SHELL_REMOTE_CONN(ppr, shell_remote_ppr, DT_NODELABEL(cpuapp_cpuppr_ipc));
#endif

#if defined(CONFIG_SHELL_REMOTE_FLPR) && DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(cpuapp_cpuflpr_ipc))
SHELL_REMOTE_CONN(flpr, shell_remote_flpr, DT_NODELABEL(cpuapp_cpuflpr_ipc));
#endif

#if defined(CONFIG_SHELL_REMOTE_RADIO) && DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(cpuapp_cpurad_ipc))
SHELL_REMOTE_CONN(radio, shell_remote_radio, DT_NODELABEL(cpuapp_cpurad_ipc));
#endif
#elif defined(CONFIG_SOC_COMPATIBLE_NRF53X)
SHELL_REMOTE_CONN(net, shell_remote_cli, DT_NODELABEL(ipc0));
#elif defined(CONFIG_NORDIC_VPR_LAUNCHER)
SHELL_REMOTE_CONN(flpr, shell_remote_flpr, DT_NODELABEL(ipc0));
#else
#error "No remote shell connection found"
#endif
