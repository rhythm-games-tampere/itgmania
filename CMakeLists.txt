cmake_minimum_required(VERSION 3.28)

# Must be set before project(); ignored on other platforms.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11" CACHE STRING "Minimum macOS version")

project(ITGmania VERSION 1.3.0 LANGUAGES C CXX)

set(SM_EXE_NAME "ITGmania")

set(SM_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SM_CMAKE_DIR "${SM_ROOT_DIR}/CMake")
set(SM_EXTERN_DIR "${SM_ROOT_DIR}/extern")
set(SM_INSTALLER_DIR "${SM_ROOT_DIR}/Installer")
set(SM_XCODE_DIR "${SM_ROOT_DIR}/Xcode")
set(SM_PROGRAM_DIR "${SM_ROOT_DIR}/Program")
set(SM_SRC_DIR "${SM_ROOT_DIR}/src")
set(SM_GENERATED_SRC_DIR "${PROJECT_BINARY_DIR}/generated/src")

# Static MSVC runtime instead of the DLL.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(CMAKE_GENERATOR STREQUAL "Xcode")
  # Makes Xcode's 'Archive' build find the static libraries.
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/extern")
endif()

include("${SM_CMAKE_DIR}/DefineOptions.cmake")
include("${SM_CMAKE_DIR}/SMDefs.cmake")

# LTO for Release and RelWithDebInfo (about 4% less CPU per frame in
# RenderTest), whatever the generator. Pass
# -DCMAKE_INTERPROCEDURAL_OPTIMIZATION_<CONFIG>=OFF to turn it off.
if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE
   OR NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT SM_IPO_SUPPORTED OUTPUT SM_IPO_ERROR)
  if(NOT SM_IPO_SUPPORTED)
    message(STATUS "Building without LTO, which the toolchain doesn't support: ${SM_IPO_ERROR}")
  endif()
  foreach(config RELEASE RELWITHDEBINFO)
    if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION_${config})
      set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_${config} ${SM_IPO_SUPPORTED})
    endif()
  endforeach()
endif()

# Profile-guided optimization of Release builds: build with GENERATE, play
# (the instrumented game writes its profile to SM_PGO_DIR on exit), then
# rebuild the same tree with USE.
set(SM_PGO "OFF" CACHE STRING "Profile-guided optimization: OFF, GENERATE or USE")
set_property(CACHE SM_PGO PROPERTY STRINGS OFF GENERATE USE)
set(SM_PGO_DIR "${PROJECT_BINARY_DIR}/pgo" CACHE PATH
    "Where the PGO profile is written (GENERATE) and read (USE)")
set(SM_PGO_COMPILE_OPTIONS "")
set(SM_PGO_LINK_OPTIONS "")
if(SM_PGO STREQUAL "GENERATE" OR SM_PGO STREQUAL "USE")
  file(MAKE_DIRECTORY "${SM_PGO_DIR}")
  if(MSVC)
    # MSVC instruments and optimizes at link time, so only the link changes,
    # and it needs link-time code generation.
    if(NOT CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE)
      message(FATAL_ERROR "SM_PGO needs LTO for Release on MSVC.")
    endif()
    if(SM_PGO STREQUAL "GENERATE")
      set(SM_PGO_LINK_OPTIONS "/GENPROFILE:PGD=${SM_PGO_DIR}/${SM_EXE_NAME}.pgd")
    else()
      set(SM_PGO_LINK_OPTIONS "/USEPROFILE:PGD=${SM_PGO_DIR}/${SM_EXE_NAME}.pgd")
    endif()
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(SM_PGO STREQUAL "GENERATE")
      # Atomic counters: the game updates them from several threads.
      set(SM_PGO_COMPILE_OPTIONS "-fprofile-generate=${SM_PGO_DIR}"
                                 "-fprofile-update=atomic")
    else()
      # Code the training run never reached is optimized as usual instead of
      # for size.
      set(SM_PGO_COMPILE_OPTIONS "-fprofile-use=${SM_PGO_DIR}"
                                 "-fprofile-partial-training"
                                 "-Wno-missing-profile")
    endif()
    set(SM_PGO_LINK_OPTIONS ${SM_PGO_COMPILE_OPTIONS})
  else()
    # ponytail: Clang needs an llvm-profdata merge step between the runs; add
    # it when macOS releases want PGO.
    message(FATAL_ERROR "SM_PGO supports MSVC and GCC only.")
  endif()
  list(TRANSFORM SM_PGO_COMPILE_OPTIONS PREPEND "$<$<CONFIG:Release>:")
  list(TRANSFORM SM_PGO_COMPILE_OPTIONS APPEND ">")
  list(TRANSFORM SM_PGO_LINK_OPTIONS PREPEND "$<$<CONFIG:Release>:")
  list(TRANSFORM SM_PGO_LINK_OPTIONS APPEND ">")
  # Every library linked into the game, not just its own sources.
  add_compile_options(${SM_PGO_COMPILE_OPTIONS})
elseif(NOT SM_PGO STREQUAL "OFF")
  message(FATAL_ERROR "SM_PGO must be OFF, GENERATE or USE, not '${SM_PGO}'.")
endif()

# Groups a vendored target in IDEs and silences its warnings.
function(sm_vendored_target target)
  set_target_properties("${target}" PROPERTIES FOLDER "External Libraries"
                                               SYSTEM ON)
  if(WITH_EXTERNAL_WARNINGS)
    return()
  endif()
  if(CMAKE_GENERATOR STREQUAL "Xcode")
    set_property(TARGET "${target}"
                 PROPERTY XCODE_ATTRIBUTE_GCC_WARN_INHIBIT_ALL_WARNINGS "YES")
  elseif(MSVC)
    target_compile_options("${target}" PRIVATE "/W0")
  else()
    target_compile_options("${target}" PRIVATE "-w")
  endif()
endfunction()

# --- Platform dependencies --------------------------------------------------

find_package(Threads REQUIRED)

if(WIN32)
  include("${SM_CMAKE_DIR}/ffmpeg-w32.cmake")
else()
  find_program(SM_NASM NAMES nasm yasm)
  if(NOT SM_NASM)
    message(FATAL_ERROR "Neither NASM nor YASM were found. Please install one of them.")
  endif()
  include("${SM_CMAKE_DIR}/SetupFfmpeg.cmake")
endif()

if(APPLE)
  find_package(Iconv REQUIRED)
elseif(LINUX OR BSD)
  find_package(PkgConfig REQUIRED)

  set(HAS_GTK3 FALSE)
  if(WITH_GTK3)
    pkg_check_modules(GTK3 IMPORTED_TARGET gtk+-3.0)
    if(GTK3_FOUND)
      set(HAS_GTK3 TRUE)
    else()
      message(STATUS "GTK3 was not found on your system. There will be no loading window.")
    endif()
  endif()

  if(WITH_X11 OR WITH_XRANDR OR WITH_LIBXTST OR WITH_XINERAMA)
    find_package(X11 REQUIRED)
  endif()
  # FindX11 only creates the targets of the parts it finds; say which is missing
  # here instead of failing later on an unknown X11::* target.
  foreach(option_part WITH_X11:Xext WITH_XRANDR:Xrandr WITH_LIBXTST:Xtst
                      WITH_XINERAMA:Xinerama)
    string(REPLACE ":" ";" option_part "${option_part}")
    list(GET option_part 0 option)
    list(GET option_part 1 part)
    if(${option} AND NOT X11_${part}_FOUND)
      string(TOLOWER "lib${part}-dev" package)
      message(FATAL_ERROR "${option} needs lib${part} (${package} on Debian/Ubuntu). "
                          "Install it, or configure with -D${option}=OFF.")
    endif()
  endforeach()
  set(HAS_X11 ${WITH_X11})
  set(HAS_XRANDR ${WITH_XRANDR})
  set(HAS_LIBXTST ${WITH_LIBXTST})
  set(HAS_XINERAMA ${WITH_XINERAMA})

  set(HAS_PULSE ${WITH_PULSEAUDIO})
  if(HAS_PULSE)
    pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
  endif()
  set(HAS_ALSA ${WITH_ALSA})
  if(HAS_ALSA)
    find_package(ALSA REQUIRED)
  endif()
  set(HAS_JACK ${WITH_JACK})
  if(HAS_JACK)
    pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)
  endif()
  if(NOT (HAS_PULSE OR HAS_ALSA OR HAS_JACK))
    message(FATAL_ERROR "No sound libraries selected. You will require at least one.")
  endif()

  set(OpenGL_GL_PREFERENCE GLVND)
  find_package(OpenGL REQUIRED)

  pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
endif()

configure_file("${SM_SRC_DIR}/verstub.cpp.in"
               "${SM_GENERATED_SRC_DIR}/verstub.cpp")

add_subdirectory(extern)
add_subdirectory(src)

include("${SM_CMAKE_DIR}/CPackSetup.cmake")
