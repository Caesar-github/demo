# vi: set noexpandtab syntax=cmake:

project(rk_npu_uvc)
include(GNUInstallDirs)

pkg_check_modules(LIBEASYMEDIA REQUIRED libeasymedia)

set(CMAKE_CXX_STANDARD 11)
add_definitions(-fno-rtti)
# add_definitions(-fexceptions -finstrument-functions -funwind-tables -g
# -rdynamic -O0)
add_definitions(-O2)
include_directories(${RKNPU_HEADER_DIR})
if(NPU_DEVICE)
  set(RK_NPU_UVC_SRC npu_uvc_device.cc)
  set(RK_NPU_UVC_TARGET rk_npu_uvc_device)
  set(RK_NPU_UVC_SCRIPT npu_uvc_device.sh)
elseif(NPU_HOST)
  set(RK_NPU_UVC_SRC npu_uvc_host.cc)
  set(RK_NPU_UVC_TARGET rk_npu_uvc_host)
  set(RK_NPU_UVC_SCRIPT npu_uvc_host.sh)
endif()

add_executable(${RK_NPU_UVC_TARGET} ${RK_NPU_UVC_SRC})
target_include_directories(${RK_NPU_UVC_TARGET}
                           PUBLIC ${LIBEASYMEDIA_INCLUDE_DIRS})

# add_definitions(-fsanitize=address -static-libasan -fno-stack-protector -fno-
# omit-frame-pointer -fno-var-tracking -g1)
# target_link_libraries(rk_image_process asan)
target_link_libraries(${RK_NPU_UVC_TARGET} easymedia rkuvc)

install(TARGETS ${RK_NPU_UVC_TARGET} RUNTIME DESTINATION "bin")
install(PROGRAMS ${RK_NPU_UVC_SCRIPT} DESTINATION "bin")
