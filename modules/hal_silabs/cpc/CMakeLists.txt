# Copyright (c) 2026 Silicon Laboratories Inc.
# SPDX-License-Identifier: Apache-2.0

set(CPC_DIR   ${ZEPHYR_HAL_SILABS_MODULE_DIR}/cpc)
set(SISDK_DIR ${ZEPHYR_HAL_SILABS_MODULE_DIR}/simplicity_sdk)

zephyr_include_directories(
  ${ZEPHYR_BASE}/include/zephyr/portability
  ${CPC_DIR}/config
  ${CPC_DIR}/inc
  ${CPC_DIR}/src
  ${SISDK_DIR}/platform/common/inc
  ${SISDK_DIR}/platform/service/memory_manager/inc
)

zephyr_compile_definitions(
  SL_CATALOG_CPC_NG_PRESENT
  SL_CATALOG_CPC_NG_PRIMARY_PRESENT
)

zephyr_library_sources(
  ${CPC_DIR}/src/sli_cpc.c
  ${CPC_DIR}/src/sli_cpc_buf.c
  ${CPC_DIR}/src/sli_cpc_bus.c
  ${CPC_DIR}/src/sli_cpc_control.c
  ${CPC_DIR}/src/sli_cpc_control_primary.c
  ${CPC_DIR}/src/sli_cpc_crc.c
  ${CPC_DIR}/src/sli_cpc_dispatcher.c
  ${CPC_DIR}/src/sli_cpc_frame.c
  ${CPC_DIR}/src/sli_cpc_memory.c
  ${CPC_DIR}/src/sli_cpc_msgq.c
  ${CPC_DIR}/src/sli_cpc_panic.c
  src/sli_cpc_timer.c
)

# Platform bits of the Simplicity SDK that the core relies on. Silicon Labs
# SoCs already build them.
if(NOT CONFIG_HAS_SILABS_SISDK AND NOT CONFIG_HAS_SILABS_WISECONNECT)
  zephyr_library_sources(${SISDK_DIR}/platform/common/src/sl_slist.c)
endif()
if(NOT CONFIG_HAS_SILABS_SISDK)
  zephyr_library_sources(../simplicity_sdk/src/sl_memory_manager_pool_shim.c)
endif()
