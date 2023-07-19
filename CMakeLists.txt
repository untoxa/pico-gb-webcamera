# cmake version
cmake_minimum_required(VERSION 3.5)

# include the sdk.cmake file
include(pico_sdk_import.cmake)

# give the project a name (anything you want)
project(pico_gb_printer_cam)

# initialize the sdk
pico_sdk_init()

#######

set(PICO_TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)

# name anything you want
add_executable(${PROJECT_NAME})

# must match with pio filename and executable name from above
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/linkcable.pio)
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/linkcable_sm.pio)

# must match with executable name and source file names
target_sources(${PROJECT_NAME} PRIVATE pico_gb_printer_cam.c usb_descriptors.c gb_printer.c linkcable.c)

target_include_directories(${PROJECT_NAME} PRIVATE ${PICO_TINYUSB_PATH}/src .)

# must match with executable name
target_link_libraries(${PROJECT_NAME} PRIVATE pico_stdlib pico_unique_id hardware_pio tinyusb_device)

# must match with executable name
pico_add_extra_outputs(${PROJECT_NAME})
