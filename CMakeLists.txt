#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ncs311_https_client_memfault_nrf7002dk)

# Generate hex files from pem files
set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/certs)
zephyr_include_directories(${gen_dir})
generate_inc_file_for_target(
    app
    cert/DigiCertGlobalG3.pem
    ${gen_dir}/DigiCertGlobalG3.pem.inc
    )

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)


# Make local Memfault config headers discoverable by the SDK
zephyr_include_directories(config)

if(CONFIG_MEMFAULT)
    target_sources(app PRIVATE
        src/mflt_services.c
        src/mflt_ota_triggers.c
        src/mflt_wifi_metrics.c
    )

    if(CONFIG_MEMFAULT_NCS_STACK_METRICS)
        target_sources(app PRIVATE src/mflt_stack_metrics.c)
        target_include_directories(app PRIVATE ${ZEPHYR_NRF_MODULE_DIR}/modules/memfault-firmware-sdk/include)
    endif()
endif()
# NORDIC SDK APP END
