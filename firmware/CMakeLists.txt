# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "1.5.1")
add_definitions(-DFIRMWARE_VERSION=\"${PROJECT_VER}\")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

# CI can keep sdkconfig and defaults outside the checkout. These environment
# variables are intentionally paths only; credentials must not be passed here.
if(DEFINED ENV{STACKCHAN_SDKCONFIG})
    set(SDKCONFIG "$ENV{STACKCHAN_SDKCONFIG}"
        CACHE FILEPATH "StackChan sdkconfig path" FORCE)
endif()

if(DEFINED ENV{STACKCHAN_SDKCONFIG_DEFAULTS})
    set(SDKCONFIG_DEFAULTS "$ENV{STACKCHAN_SDKCONFIG_DEFAULTS}"
        CACHE STRING "StackChan sdkconfig default files" FORCE)
    message(STATUS "StackChan: applying explicit sdkconfig defaults")
# Auto-load a local sdkconfig overlay (git-ignored) when present. Lets
# self-hosted / custom deployments pin CONFIG_STACKCHAN_SERVER_URL,
# CONFIG_OTA_URL, etc. without touching committed defaults.
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.local")
    set(SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.local"
        CACHE STRING "ESP-IDF sdkconfig default files (auto-extended with local overlay)" FORCE)
    message(STATUS "StackChan: detected sdkconfig.defaults.local, applying overlay")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stack-chan)
