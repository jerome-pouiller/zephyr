# Copyright (c) 2024 Silicon Laboratories Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)
zephyr_sources(siwx91x_nwp.c siwx91x_nwp_bus.c siwx91x_nwp_api.c siwx91x_nwp_fw.c)
zephyr_code_relocate(FILES siwx91x_nwp_bus.c LOCATION RAM)
zephyr_compile_definitions(SLI_SI91X_ENABLE_OS)
