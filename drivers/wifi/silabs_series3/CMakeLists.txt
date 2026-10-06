# Copyright (c) 2026 Silicon Laboratories Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(wifi_series3.c wifi_series3_supp.c)
zephyr_library_link_libraries(hostap)
