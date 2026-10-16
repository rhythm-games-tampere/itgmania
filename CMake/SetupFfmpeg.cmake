set(SM_FFMPEG_SRC_DIR "${SM_EXTERN_DIR}/ffmpeg")

list(APPEND FFMPEG_CONFIGURE
            "${SM_FFMPEG_SRC_DIR}/configure"
            "--disable-autodetect"
            "--disable-avdevice"
            "--disable-avfilter"
            "--disable-devices"
            "--disable-doc"
            "--disable-filters"
            "--disable-lzma"
            "--disable-network"
            "--disable-postproc"
            "--disable-programs"
            "--disable-swresample"
            "--disable-vaapi"
            "--disable-bzlib"
            "--enable-gpl"
            "--enable-version3"
            "--enable-pthreads"
            "--enable-static"
            "--enable-zlib"
            "--prefix=/")

set(FFMPEG_CC "${CMAKE_C_COMPILER}")
set(FFMPEG_CXX "${CMAKE_CXX_COMPILER}")
if(CMAKE_C_COMPILER_LAUNCHER)
  string(REPLACE ";" " " FFMPEG_C_LAUNCHER "${CMAKE_C_COMPILER_LAUNCHER}")
  set(FFMPEG_CC "${FFMPEG_C_LAUNCHER} ${FFMPEG_CC}")
endif()
if(CMAKE_CXX_COMPILER_LAUNCHER)
  string(REPLACE ";" " " FFMPEG_CXX_LAUNCHER "${CMAKE_CXX_COMPILER_LAUNCHER}")
  set(FFMPEG_CXX "${FFMPEG_CXX_LAUNCHER} ${FFMPEG_CXX}")
endif()
list(APPEND FFMPEG_CONFIGURE "--cc=${FFMPEG_CC}" "--cxx=${FFMPEG_CXX}")

if(CMAKE_C_FLAGS)
  list(APPEND FFMPEG_CONFIGURE "--extra-cflags=${CMAKE_C_FLAGS}")
endif()
if(CMAKE_CXX_FLAGS)
  list(APPEND FFMPEG_CONFIGURE "--extra-cxxflags=${CMAKE_CXX_FLAGS}")
endif()

if(CMAKE_POSITION_INDEPENDENT_CODE)
  list(APPEND FFMPEG_CONFIGURE "--enable-pic")
endif()

if(APPLE)
  list(APPEND FFMPEG_CONFIGURE "--enable-cross-compile")
  list(APPEND FFMPEG_CONFIGURE "--enable-videotoolbox")
  list(APPEND FFMPEG_CONFIGURE "--extra-cflags=-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
  if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    list(APPEND FFMPEG_CONFIGURE "--arch=arm64" "--extra-cflags=-arch arm64" "--extra-ldflags=-arch arm64")
  elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    list(APPEND FFMPEG_CONFIGURE "--arch=x86_64" "--extra-cflags=-arch x86_64" "--extra-ldflags=-arch x86_64")
  else()
    message(FATAL_ERROR
      "Unsupported macOS architecture: ${CMAKE_OSX_ARCHITECTURES}, set CMAKE_OSX_ARCHITECTURES to either arm64 or x86_64"
    )
  endif()
endif()

if(NOT WITH_EXTERNAL_WARNINGS)
  list(APPEND FFMPEG_CONFIGURE "--extra-cflags=-w")
endif()

if(CMAKE_GENERATOR MATCHES "Make")
  # Shares the parent make's job slots.
  set(SM_FFMPEG_MAKE "make")
else()
  # ponytail: Ninja and Xcode don't hand out job slots, so ffmpeg runs its own
  # -j next to them; a jobserver-aware Ninja (1.13+) could share instead.
  if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
    set(NUM_CORES $ENV{CMAKE_BUILD_PARALLEL_LEVEL})
  else()
    cmake_host_system_information(RESULT NUM_CORES QUERY NUMBER_OF_LOGICAL_CORES)
  endif()
  set(SM_FFMPEG_MAKE "make" "-j${NUM_CORES}")
endif()

set(SM_FFMPEG_LIBS avformat avcodec swscale avutil)
list(TRANSFORM SM_FFMPEG_LIBS REPLACE "(.+)" "<BINARY_DIR>/dest/lib/lib\\1.a"
     OUTPUT_VARIABLE SM_FFMPEG_BYPRODUCTS)

include(ExternalProject)
ExternalProject_Add("ffmpeg-build"
                    SOURCE_DIR "${SM_FFMPEG_SRC_DIR}"
                    CONFIGURE_COMMAND ${FFMPEG_CONFIGURE}
                    BUILD_COMMAND ${SM_FFMPEG_MAKE} COMMAND make DESTDIR=./dest install
                    BUILD_JOB_SERVER_AWARE TRUE
                    UPDATE_COMMAND ""
                    INSTALL_COMMAND ""
                    TEST_COMMAND ""
                    BUILD_BYPRODUCTS ${SM_FFMPEG_BYPRODUCTS})

ExternalProject_Get_Property("ffmpeg-build" BINARY_DIR)
# The include dir only exists after the build; create it so CMake accepts it.
file(MAKE_DIRECTORY "${BINARY_DIR}/dest/include")

# Imported targets so the linker sees each library before what it depends on.
foreach(lib IN LISTS SM_FFMPEG_LIBS)
  add_library(ffmpeg::${lib} STATIC IMPORTED)
  set_target_properties(ffmpeg::${lib} PROPERTIES
                        IMPORTED_LOCATION "${BINARY_DIR}/dest/lib/lib${lib}.a")
  add_dependencies(ffmpeg::${lib} "ffmpeg-build")
endforeach()
target_link_libraries(ffmpeg::avformat INTERFACE ffmpeg::avcodec)
target_link_libraries(ffmpeg::avcodec INTERFACE ffmpeg::avutil ZLIB::ZLIBSTATIC)
target_link_libraries(ffmpeg::swscale INTERFACE ffmpeg::avutil)
target_link_libraries(ffmpeg::avutil INTERFACE Threads::Threads)
if(APPLE)
  target_link_libraries(ffmpeg::avutil INTERFACE
                        "$<LINK_LIBRARY:FRAMEWORK,CoreMedia>"
                        "$<LINK_LIBRARY:FRAMEWORK,CoreVideo>"
                        "$<LINK_LIBRARY:FRAMEWORK,VideoToolbox>")
endif()

add_library(ffmpeg INTERFACE)
target_include_directories(ffmpeg SYSTEM INTERFACE "${BINARY_DIR}/dest/include")
target_link_libraries(ffmpeg INTERFACE ffmpeg::avformat ffmpeg::swscale)
