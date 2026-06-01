cmake_minimum_required(VERSION 3.16)
project(MixerClientLinux CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

# HID access via hidraw backend (preferred on Linux — no libusb needed)
pkg_check_modules(HIDAPI REQUIRED hidapi-hidraw)

# PulseAudio for per-application volume control
pkg_check_modules(PULSE REQUIRED libpulse)

# X11 for focused-window detection
pkg_check_modules(X11 REQUIRED x11)

# nlohmann/json for settings file
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    # Fall back to FetchContent if not installed system-wide
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_executable(MixerClientLinux
    MixerClient.cpp
    HidReader.cpp
    VolumeMixerController.cpp
)

target_include_directories(MixerClientLinux PRIVATE
    ${HIDAPI_INCLUDE_DIRS}
    ${PULSE_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
)

target_compile_options(MixerClientLinux PRIVATE
    ${HIDAPI_CFLAGS_OTHER}
    ${PULSE_CFLAGS_OTHER}
)

target_link_libraries(MixerClientLinux PRIVATE
    ${HIDAPI_LIBRARIES}
    ${PULSE_LIBRARIES}
    ${X11_LIBRARIES}
    nlohmann_json::nlohmann_json
)

# Needed to open /dev/hidraw* without root:
# sudo usermod -aG plugdev $USER
# and a udev rule for your device, e.g.:
#   SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0424", ATTRS{idProduct}=="5734", MODE="0660", GROUP="plugdev"
