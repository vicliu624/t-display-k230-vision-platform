cmake_minimum_required(VERSION 3.10)

project(vicliu-pocket-linux-hardware VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(VPL_WARNING_FLAGS -Wall -Wextra -Werror)

add_library(vpl-hardware-core STATIC
    hardware/battery.cpp
    hardware/controls.cpp
    hardware/daemon.cpp
    hardware/dock.cpp
    hardware/lr2021.cpp
    hardware/network.cpp
    hardware/nrf9151.cpp
    hardware/paths.cpp
    hardware/quick_settings_service.cpp
    hardware/status.cpp
)
target_compile_features(vpl-hardware-core PUBLIC cxx_std_17)
target_compile_options(vpl-hardware-core PRIVATE ${VPL_WARNING_FLAGS})
target_include_directories(vpl-hardware-core PUBLIC hardware)

add_executable(vpl-hwctl main.cpp)
target_compile_features(vpl-hwctl PRIVATE cxx_std_17)
target_compile_options(vpl-hwctl PRIVATE ${VPL_WARNING_FLAGS})
target_link_libraries(vpl-hwctl PRIVATE vpl-hardware-core)

add_executable(vpl-hardwared daemon-main.cpp)
target_compile_features(vpl-hardwared PRIVATE cxx_std_17)
target_compile_options(vpl-hardwared PRIVATE ${VPL_WARNING_FLAGS})
target_link_libraries(vpl-hardwared PRIVATE vpl-hardware-core)

add_executable(vpl-lora-probe lora-probe-main.cpp)
target_compile_features(vpl-lora-probe PRIVATE cxx_std_17)
target_compile_options(vpl-lora-probe PRIVATE ${VPL_WARNING_FLAGS})
target_link_libraries(vpl-lora-probe PRIVATE vpl-hardware-core)

add_executable(vpl-nrf9151-probe nrf9151-probe-main.cpp)
target_compile_features(vpl-nrf9151-probe PRIVATE cxx_std_17)
target_compile_options(vpl-nrf9151-probe PRIVATE ${VPL_WARNING_FLAGS})
target_link_libraries(vpl-nrf9151-probe PRIVATE vpl-hardware-core)

install(TARGETS vpl-hwctl vpl-lora-probe vpl-nrf9151-probe RUNTIME DESTINATION local/bin)
install(TARGETS vpl-hardwared RUNTIME DESTINATION libexec/vicliu-pocket-linux-hardware)
install(PROGRAMS
    tdvp-audio-route
    tdvp-expand-rootfs
    tdvp-speaker-acceptance
    DESTINATION local/libexec/vicliu-pocket-linux-hardware)
install(PROGRAMS
    tdvp-audio-route
    tdvp-speaker-acceptance
    DESTINATION local/bin)
install(FILES
    vicliu-pocket-linux-hardware.service
    tdvp-rootfs-expand.service
    tdvp-rtc-load.service
    tdvp-rtc-restore.service
    tdvp-rtc-writeback.service
    tdvp-rtc-writeback.timer
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
install(FILES tdvp-timesyncd.conf
    DESTINATION /etc/systemd/timesyncd.conf.d)
install(FILES hardware.conf DESTINATION /etc/vicliu-pocket-linux-hardware)
