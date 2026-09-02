cmake_minimum_required(VERSION 3.10)

project(vicliu-pocket-linux-hardware VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)

set(VPL_WARNING_FLAGS -Wall -Wextra -Werror)

add_library(vpl-hardware-core STATIC
    hardware/battery.cpp
    hardware/bluetooth.cpp
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
target_link_libraries(vpl-hardware-core PUBLIC PkgConfig::DBUS)

# CPU1 is not a Linux scheduler CPU on K230.  This deliberately small C ABI
# maps only the dedicated non-cacheable mailbox provided by the Linux driver;
# it is suitable for applications that dispatch bounded jobs to RT-Smart.
add_library(tdvp_cpu1 SHARED tdvp_cpu1.c)
set_target_properties(tdvp_cpu1 PROPERTIES VERSION 1.0.0 SOVERSION 1)
target_compile_features(tdvp_cpu1 PRIVATE c_std_11)
target_compile_options(tdvp_cpu1 PRIVATE ${VPL_WARNING_FLAGS})
target_include_directories(tdvp_cpu1 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(tdvp-cpu1ctl tdvp-cpu1ctl.c)
target_compile_features(tdvp-cpu1ctl PRIVATE c_std_11)
target_compile_options(tdvp-cpu1ctl PRIVATE ${VPL_WARNING_FLAGS})
target_link_libraries(tdvp-cpu1ctl PRIVATE tdvp_cpu1)

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

install(TARGETS vpl-hwctl vpl-lora-probe vpl-nrf9151-probe tdvp-cpu1ctl RUNTIME DESTINATION local/bin)
install(TARGETS vpl-hardwared RUNTIME DESTINATION libexec/vicliu-pocket-linux-hardware)
install(TARGETS tdvp_cpu1 LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES tdvp_cpu1.h tdvp_cpu1_abi.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
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
