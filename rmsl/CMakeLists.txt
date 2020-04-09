cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)
PROJECT(rmsl_linux_demo)

add_definitions(${CMAKE_CXX_FLAGS} "-Wno-sign-compare")

include_directories(./)
include_directories(${CMAKE_SYSROOT}/usr/include/libdrm/)

set(DEMO_SOURCE
    main.c
    rmsl_ctrl.c
    rmsl_depth.c
    vpu_decode.c
    rkdrm_display.c
)

ADD_EXECUTABLE(rmsl_linux_demo ${DEMO_SOURCE})
target_link_libraries(rmsl_linux_demo rkisp_api rockchip_mpp rga drm)

set(TOOL_SOURCE
    rmsl_ctrl.c
    rmsl_tool.c
)

ADD_EXECUTABLE(rmsl_tool ${TOOL_SOURCE})
target_link_libraries(rmsl_tool rkisp_api)

install(TARGETS rmsl_linux_demo DESTINATION bin)
install(TARGETS rmsl_tool DESTINATION bin)
install(DIRECTORY ./rmsl DESTINATION include
        FILES_MATCHING PATTERN "rmsl_api.h")
