/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Replacement for the logger helper of the Simplicity SDK: routes the CPC
 * logs (SLI_CPC_LOG_*) to the Zephyr logging subsystem, under the silabs_cpc
 * module. The CPC core sources are built with SLI_CPC_LOG_MODULE defined and
 * get the module declaration from here.
 */

#ifndef SL_LOG_HELPER_H
#define SL_LOG_HELPER_H

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#ifdef SLI_CPC_LOG_MODULE
LOG_MODULE_DECLARE(silabs_cpc, CONFIG_SILABS_CPC_LOG_LEVEL);
#endif

/* Level nomenclature of the Simplicity SDK logger */
#define SL_LOG_CONFIG_LEVEL_DEBUG 1
#define SL_LOG_CONFIG_LEVEL_INFO  2
#define SL_LOG_CONFIG_LEVEL_WARN  3
#define SL_LOG_CONFIG_LEVEL_ERROR 4
#define SL_LOG_CONFIG_LEVEL_CRASH 5
#define SL_LOG_CONFIG_LEVEL_NONE  6
/* Filtering is done by Zephyr, let everything through here */
#define SL_LOG_CONFIG_LEVEL_COMPILE_TIME SL_LOG_CONFIG_LEVEL_DEBUG

#define SLI_CPC_ZEPHYR_LOG_ERR(...)   LOG_ERR(__VA_ARGS__)
#define SLI_CPC_ZEPHYR_LOG_WRN(...)   LOG_WRN(__VA_ARGS__)
#define SLI_CPC_ZEPHYR_LOG_INFO(...)  LOG_INF(__VA_ARGS__)
#define SLI_CPC_ZEPHYR_LOG_DBG(...)   LOG_DBG(__VA_ARGS__)
#define SLI_CPC_ZEPHYR_LOG_CRASH(...) LOG_ERR(__VA_ARGS__)

#define sl_printf_common(level, ...) SLI_CPC_ZEPHYR_LOG_##level(__VA_ARGS__)

/* Called before sli_cpc_panic(): make sure the messages get out */
#define sl_log_flush() LOG_PANIC()

#endif /* SL_LOG_HELPER_H */
