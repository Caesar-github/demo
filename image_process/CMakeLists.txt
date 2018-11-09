# vi: set noexpandtab syntax=cmake:

PROJECT(rk_image_process)
INCLUDE(GNUInstallDirs)

find_package(gflags REQUIRED)
pkg_check_modules(LIBAVUTIL REQUIRED libavutil>=55.78.100)
pkg_check_modules(LIBDRM REQUIRED libdrm>=2.4.89)

set (CMAKE_CXX_STANDARD 11)

# add_definitions(-fexceptions -finstrument-functions -funwind-tables -g -rdynamic -O0)
add_definitions(-O2)

if (DEFINED SDL_GLES2FUNCS_H)
    add_definitions(-DSDL_GLES2FUNCS_H=${SDL_GLES2FUNCS_H})
endif()

set(RK_IMAGE_PROCESS_SRC image_process.cc)

add_executable(rk_image_process ${RK_IMAGE_PROCESS_SRC})

target_include_directories(rk_image_process
                           PUBLIC ${GFLAGS_INCLUDE_DIRS}
                           PUBLIC ${LIBAVUTIL_INCLUDE_DIRS}
                           PUBLIC ${LIBDRM_INCLUDE_DIRS}
                          )
target_link_libraries(rk_image_process drm gflags avutil avformat avcodec rga
                                       SDL2 SDL2_ttf rknncascade)

install(TARGETS rk_image_process RUNTIME DESTINATION "bin")
