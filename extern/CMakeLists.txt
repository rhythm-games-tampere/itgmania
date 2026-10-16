# Vendored third-party libraries, all linked statically into the game.
set(BUILD_SHARED_LIBS OFF)
# Honour the LTO presets in subprojects that declare an older CMake minimum.
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

# --- Libraries built with their own CMake project ---------------------------

# SDL 3. Only the subsystems the game uses are built: video (window, GL
# context, display modes, events) and joysticks.
set(SDL_SHARED OFF CACHE INTERNAL "")
set(SDL_STATIC ON CACHE INTERNAL "")
set(SDL_STATIC_PIC ON CACHE INTERNAL "")
set(SDL_INSTALL OFF CACHE INTERNAL "")
set(SDL_TEST_LIBRARY OFF CACHE INTERNAL "")
set(SDL_TESTS OFF CACHE INTERNAL "")
set(SDL_EXAMPLES OFF CACHE INTERNAL "")
foreach(subsystem AUDIO GPU RENDER CAMERA HAPTIC HIDAPI POWER SENSOR DIALOG)
  set(SDL_${subsystem} OFF CACHE INTERNAL "")
endforeach()
set(SDL_JOYSTICK ON CACHE INTERNAL "")
# ponytail: only used to suspend the screensaver, which SDL also does without
# it; keeps libxss-dev off the Linux build dependencies.
set(SDL_X11_XSCRNSAVER OFF CACHE INTERNAL "")
add_subdirectory(SDL EXCLUDE_FROM_ALL SYSTEM)

set(ENABLE_TESTING OFF CACHE INTERNAL "Don't build mbedtls tests")
set(GEN_FILES OFF CACHE INTERNAL "Don't generate files (requires perl and python)")
add_subdirectory(mbedtls EXCLUDE_FROM_ALL SYSTEM)
if(CMAKE_GENERATOR STREQUAL "Xcode")
  target_compile_options(mbedcrypto PRIVATE -Wno-shorten-64-to-32)
endif()

set(ZLIB_BUILD_SHARED OFF CACHE INTERNAL "")
set(ZLIB_BUILD_TESTING OFF CACHE INTERNAL "")
set(ZLIB_INSTALL OFF CACHE INTERNAL "")
add_subdirectory(zlib EXCLUDE_FROM_ALL SYSTEM)

set(HIDAPI_WITH_LIBUSB OFF CACHE INTERNAL "")
set(HIDAPI_INSTALL_TARGETS OFF CACHE INTERNAL "")
set(HIDAPI_WITH_TESTS OFF CACHE INTERNAL "")
set(HIDAPI_BUILD_HIDTEST OFF CACHE INTERNAL "")
set(HIDAPI_BUILD_PP_DATA_DUMP OFF CACHE INTERNAL "")
add_subdirectory(hidapi EXCLUDE_FROM_ALL SYSTEM)

if(LINUX)
  set(LIBUSB_BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
  set(LIBUSB_INSTALL_TARGETS OFF CACHE INTERNAL "")
  add_subdirectory(libusb EXCLUDE_FROM_ALL SYSTEM)
endif()

set_property(TARGET SDL3-static mbedtls mbedcrypto mbedx509
             PROPERTY FOLDER "External Libraries")
# These were always built quietly; upstream zlib, hidapi and libusb do warn.
get_target_property(SM_HIDAPI_TARGET hidapi::hidapi ALIASED_TARGET)
sm_vendored_target(zlibstatic)
sm_vendored_target("${SM_HIDAPI_TARGET}")
if(LINUX)
  sm_vendored_target(usb-1.0)
endif()

# --- Libraries built from hand-written source lists -------------------------

add_library("glad" STATIC "glad/src/gl.c" "glad/include/glad/gl.h"
                          "glad/include/KHR/khrplatform.h")
# Generated with glad2 2.0.8:
#   glad --api gl:compatibility=4.6 --extensions GL_ARB_buffer_storage,GL_ARB_sync,GL_ARB_framebuffer_object,GL_ARB_vertex_array_bgra,GL_EXT_vertex_array_bgra,GL_EXT_texture_filter_anisotropic,GL_ARB_texture_float,GL_EXT_bgra,GL_KHR_debug,GL_ARB_timer_query --out-path extern/glad c
target_include_directories("glad" PUBLIC "glad/include")

add_library("jsoncpp" STATIC "jsoncpp/jsoncpp.cpp" "jsoncpp/json/json-forwards.h"
                             "jsoncpp/json/json.h")
set_target_properties("jsoncpp" PROPERTIES CXX_STANDARD 11
                                           CXX_STANDARD_REQUIRED ON
                                           CXX_EXTENSIONS OFF)
target_include_directories("jsoncpp" PUBLIC "jsoncpp")

add_library("miniz" STATIC "miniz/miniz.c" "miniz/miniz.h")
target_include_directories("miniz" PUBLIC "miniz")

add_library("stb_image" STATIC "stb_image.c" "stb/stb_image.h")
target_include_directories("stb_image" PUBLIC "stb")
if(WIN32)
  target_compile_definitions("stb_image" PUBLIC STBI_WINDOWS_UTF8)
endif()

add_library("stb_image_write" STATIC "stb_image_write.c" "stb/stb_image_write.h")
target_include_directories("stb_image_write" PUBLIC "stb")

configure_file("config.ogg.types.in.h" "ogg/ogg/config_types.h")
add_library("ogg" STATIC "ogg/src/bitwise.c" "ogg/src/framing.c"
                         "ogg/include/ogg/ogg.h" "ogg/include/ogg/os_types.h")
target_include_directories("ogg" PUBLIC "ogg/include"
                                        "${CMAKE_CURRENT_BINARY_DIR}/ogg")

add_library("vorbisfile" STATIC "vorbis/lib/vorbisfile.c"
                                "vorbis/include/vorbis/vorbisfile.h")
target_include_directories("vorbisfile" PRIVATE "vorbis/lib")
target_link_libraries("vorbisfile" PUBLIC "vorbis")

include(CMakeProject-lua.cmake)
add_library("lsqlite3" STATIC "lsqlite3_v096/lsqlite3.c" "lsqlite3_v096/sqlite3.c")
target_include_directories("lsqlite3" PUBLIC "lsqlite3_v096")
target_link_libraries("lsqlite3" PUBLIC "lua-5.1")

include(CMakeProject-mad.cmake)
include(CMakeProject-vorbis.cmake)
include(CMakeProject-pcre.cmake)
include(CMakeProject-tomcrypt.cmake)
add_subdirectory(tommath-build tommath)
include(CMakeProject-ixwebsocket.cmake)

set(SM_VENDORED_TARGETS glad jsoncpp miniz stb_image stb_image_write ogg
    vorbisfile lua-5.1 lsqlite3 mad vorbis pcre tomcrypt ixwebsocket)
foreach(target IN LISTS SM_VENDORED_TARGETS)
  sm_vendored_target("${target}")
endforeach()
