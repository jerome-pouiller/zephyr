# Copyright (c) 2024 Silicon Laboratories Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_sources_ifdef(CONFIG_SOC_SERIES_SIWG917 soc.c)
if(CONFIG_SILABS_SIWX91X_NWP)
  zephyr_sources(siwx91x_nwp.c region_db_config.c hal.c hal_ram.c)
  zephyr_code_relocate(FILES hal_ram.c LOCATION RAM)
  zephyr_compile_definitions(SLI_SI91X_ENABLE_OS)
endif()

zephyr_sources_ifdef(CONFIG_SOC_SIWX91X_PM_BACKEND_PMGR soc_siwx91x_power_pmgr.c)

set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld CACHE INTERNAL "")
