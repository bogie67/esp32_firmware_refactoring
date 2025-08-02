cmake_minimum_required(VERSION 3.16)

# Add components directory
set(EXTRA_COMPONENT_DIRS "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_firmware_refactoring)