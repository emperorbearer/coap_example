cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(nrf54_coap_lwm2m_scd40
    VERSION 1.0.0
    DESCRIPTION "nRF54 CoAP/LWM2M air quality monitor using SCD40"
)

target_sources(app PRIVATE
    src/main.c
    src/scd40.c
)

target_include_directories(app PRIVATE src)
