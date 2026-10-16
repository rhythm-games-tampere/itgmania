if(MSVC)
  # Converts the linker map into a .vdi for the crash handler's stack traces.
  include("CMakeProject-mapconv.cmake")
  if(WITH_TEXTURE_GENERATOR OR WITH_FULL_RELEASE)
    include("CMakeProject-texture.cmake")
  endif()
endif()

if(CMAKE_GENERATOR STREQUAL "Xcode")
  set(CMAKE_XCODE_GENERATE_SCHEME ON)
endif()

add_executable("${SM_EXE_NAME}")

include(CMakeData-arch.cmake)
include(CMakeData-rage.cmake)
include(CMakeData-os.cmake)
include(CMakeData-actor.cmake)
include(CMakeData-screen.cmake)
include(CMakeData-data.cmake)
include(CMakeData-file-types.cmake)
include(CMakeData-globals.cmake)
include(CMakeData-singletons.cmake)

if(APPLE)
  set_target_properties("${SM_EXE_NAME}" PROPERTIES
                        MACOSX_BUNDLE ON
                        MACOSX_BUNDLE_INFO_PLIST "${SM_XCODE_DIR}/Info.plist.in")
else()
  target_sources("${SM_EXE_NAME}" PRIVATE "Main.cpp")
endif()

# IDE folders follow the directory layout; generated files go to the default.
get_target_property(SM_SOURCES "${SM_EXE_NAME}" SOURCES)
list(FILTER SM_SOURCES EXCLUDE REGEX "^${SM_GENERATED_SRC_DIR}")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SM_SOURCES})

if(MSVC OR APPLE)
  set(SM_NAME_RELEASE "ITGmania")
else()
  set(SM_NAME_RELEASE "itgmania")
endif()

if(WIN32)
  set(SM_OUTPUT_DIR "${SM_PROGRAM_DIR}")
else()
  set(SM_OUTPUT_DIR "${SM_ROOT_DIR}")
endif()

set_target_properties("${SM_EXE_NAME}" PROPERTIES
                      CXX_STANDARD 17
                      CXX_STANDARD_REQUIRED ON
                      CXX_EXTENSIONS ON
                      FOLDER "Internal Libraries"
                      OUTPUT_NAME "${SM_NAME_RELEASE}"
                      OUTPUT_NAME_DEBUG "${SM_NAME_RELEASE}-debug"
                      OUTPUT_NAME_MINSIZEREL "${SM_NAME_RELEASE}-min-size"
                      OUTPUT_NAME_RELWITHDEBINFO "${SM_NAME_RELEASE}-release-symbols"
                      # The genex keeps multi-config generators from adding a
                      # per-config subdirectory: the game runs from here.
                      RUNTIME_OUTPUT_DIRECTORY "$<1:${SM_OUTPUT_DIR}>")

target_include_directories("${SM_EXE_NAME}" PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${SM_GENERATED_SRC_DIR}")

target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                           $<$<CONFIG:Debug>:DEBUG>
                           $<$<CONFIG:Release>:RELEASE>
                           $<$<CONFIG:MinSizeRel>:MINSIZEREL>
                           $<$<CONFIG:RelWithDebInfo>:RELWITHDEBINFO>)

target_compile_options("${SM_EXE_NAME}" PRIVATE
  $<$<CXX_COMPILER_ID:GNU>:
    -Wall -Wextra -Wno-unused -Wno-unused-parameter -Wno-unknown-pragmas -Werror=type-limits>
  $<$<CXX_COMPILER_ID:Clang>:
    -Wall -Wextra -Wno-unused -Wno-unused-parameter -Wno-unknown-pragmas -Wno-undefined-var-template>
  $<$<CXX_COMPILER_ID:AppleClang>:
    -Wall -Wextra -Wno-unused -Wno-unused-parameter -Wno-unknown-pragmas -Wno-undefined-var-template -Wno-deprecated-declarations -Wno-unused-command-line-argument>
  $<$<CXX_COMPILER_ID:MSVC>:
    /W4 /wd4100 /wd4189 /wd4201 /wd4244 /wd4267 /wd4702>
  $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C,CXX>>:/permissive- /utf-8>)
if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
  # Ninja already compiles files in parallel.
  target_compile_options("${SM_EXE_NAME}" PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/MP>)
endif()

target_link_libraries("${SM_EXE_NAME}" PRIVATE
                      "lua-5.1" "miniz" "mad" "pcre" "glad" "tomcrypt" "tommath"
                      "stb_image" "stb_image_write" "jsoncpp" "vorbisfile" "vorbis"
                      "ogg" "ixwebsocket" "hidapi::hidapi" "lsqlite3"
                      "SDL3::SDL3-static" "ffmpeg" ZLIB::ZLIBSTATIC
                      Threads::Threads)

target_link_options("${SM_EXE_NAME}" PRIVATE ${SM_PGO_LINK_OPTIONS})
if(MSVC AND SM_PGO STREQUAL "GENERATE")
  # The instrumented game loads the PGO runtime from next to itself.
  get_filename_component(SM_MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
  find_file(SM_PGORT_DLL pgort140.dll HINTS "${SM_MSVC_BIN_DIR}" NO_DEFAULT_PATH
            REQUIRED)
  add_custom_command(TARGET "${SM_EXE_NAME}" POST_BUILD
                     COMMAND "${CMAKE_COMMAND}" -E copy "${SM_PGORT_DLL}"
                             "$<TARGET_FILE_DIR:${SM_EXE_NAME}>")
elseif(MSVC AND SM_PGO STREQUAL "USE")
  add_custom_command(TARGET "${SM_EXE_NAME}" PRE_LINK
                     COMMAND "${CMAKE_COMMAND}"
                             "-DEXE_DIR=$<TARGET_FILE_DIR:${SM_EXE_NAME}>"
                             "-DNAME=$<TARGET_FILE_BASE_NAME:${SM_EXE_NAME}>"
                             "-DPGO_DIR=${SM_PGO_DIR}"
                             -P "${SM_CMAKE_DIR}/CollectPgc.cmake")
endif()

if(WIN32)
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                             WINDOWS
                             _WINDOWS
                             _CRT_SECURE_NO_WARNINGS
                             _WINSOCK_DEPRECATED_NO_WARNINGS
                             NOMINMAX
                             WIN32_LEAN_AND_MEAN
                             VC_EXTRALEAN)
  target_link_libraries("${SM_EXE_NAME}" PRIVATE dbghelp setupapi hid)

  if(MSVC)
    target_link_options("${SM_EXE_NAME}" PRIVATE
                        # mapconv turns the map into the crash handler's .vdi.
                        /MAP /MAPINFO:EXPORTS
                        $<$<CONFIG:Release,MinSizeRel>:/SUBSYSTEM:WINDOWS>)

    add_custom_command(
      TARGET "${SM_EXE_NAME}" POST_BUILD
      COMMAND "mapconv"
              "$<TARGET_FILE_DIR:${SM_EXE_NAME}>/$<TARGET_FILE_BASE_NAME:${SM_EXE_NAME}>.map"
              "$<TARGET_FILE_DIR:${SM_EXE_NAME}>/$<TARGET_FILE_BASE_NAME:${SM_EXE_NAME}>.vdi"
      COMMENT "Generating file to allow for easier stack traces.")
  endif()

  # The ffmpeg DLLs have to sit next to the executable. Each copy is a build
  # output, so a deleted one comes back on the next build without a relink.
  set(SM_FFMPEG_DLL_COPIES "")
  foreach(lib avcodec avformat avutil swscale)
    get_target_property(dll ffmpeg::${lib} IMPORTED_LOCATION)
    cmake_path(GET dll FILENAME dll_name)
    add_custom_command(OUTPUT "${SM_OUTPUT_DIR}/${dll_name}"
                       COMMAND "${CMAKE_COMMAND}" -E copy "${dll}" "${SM_OUTPUT_DIR}"
                       DEPENDS "${dll}")
    list(APPEND SM_FFMPEG_DLL_COPIES "${SM_OUTPUT_DIR}/${dll_name}")
  endforeach()
  add_custom_target("ffmpeg-dlls" DEPENDS ${SM_FFMPEG_DLL_COPIES})
  set_property(TARGET "ffmpeg-dlls" PROPERTY FOLDER "Internal Libraries")
  add_dependencies("${SM_EXE_NAME}" "ffmpeg-dlls")
elseif(APPLE)
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                             BACKTRACE_METHOD_GENERIC_UNIX
                             BACKTRACE_METHOD_TEXT="generic unix backtrace"
                             BACKTRACE_LOOKUP_METHOD_TEXT="dladdr"
                             BACKTRACE_LOOKUP_METHOD_DLADDR
                             MACOSX
                             _XOPEN_SOURCE)
  target_include_directories("${SM_EXE_NAME}" PRIVATE "archutils/Unix")
  foreach(framework Accelerate Carbon Cocoa IOKit OpenGL CoreFoundation
                    AudioToolbox AudioUnit CoreAudio CoreServices System)
    target_link_libraries("${SM_EXE_NAME}" PRIVATE
                          "$<LINK_LIBRARY:FRAMEWORK,${framework}>")
  endforeach()
  target_link_libraries("${SM_EXE_NAME}" PRIVATE Iconv::Iconv)
else() # Linux / BSD
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE UNIX HAVE_LIBPTHREAD)
  if(LINUX)
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE LINUX)
  endif()
  if(BSD)
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE BSD)
  endif()

  if(WITH_CRASH_HANDLER AND LINUX)
    # Used only in ArchHooks_Unix.cpp.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86")
      target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                                 BACKTRACE_METHOD_X86_LINUX
                                 BACKTRACE_METHOD_TEXT="x86 custom backtrace")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
      target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                                 BACKTRACE_METHOD_GENERIC_UNIX
                                 BACKTRACE_METHOD_TEXT="generic unix backtrace")
    else()
      target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                                 BACKTRACE_METHOD_TEXT="no backtrace")
    endif()
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                               BACKTRACE_LOOKUP_METHOD_TEXT="dladdr"
                               BACKTRACE_LOOKUP_METHOD_DLADDR)
  endif()

  target_link_libraries("${SM_EXE_NAME}" PRIVATE
                        ${CMAKE_DL_LIBS} OpenGL::OpenGL OpenGL::GLX)
  if(HAS_ALSA)
    target_link_libraries("${SM_EXE_NAME}" PRIVATE ALSA::ALSA)
  endif()
  if(HAS_PULSE)
    target_link_libraries("${SM_EXE_NAME}" PRIVATE PkgConfig::PULSE)
  endif()
  if(HAS_JACK)
    target_link_libraries("${SM_EXE_NAME}" PRIVATE PkgConfig::JACK)
  endif()
  if(HAS_X11)
    # Xext for DPMS in X11Helper.cpp.
    target_link_libraries("${SM_EXE_NAME}" PRIVATE X11::X11 X11::Xext)
  endif()
  if(HAS_LIBXTST)
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE HAVE_LIBXTST)
    target_link_libraries("${SM_EXE_NAME}" PRIVATE X11::Xtst)
  endif()
  if(HAS_XRANDR)
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE HAVE_XRANDR)
    target_link_libraries("${SM_EXE_NAME}" PRIVATE X11::Xrandr)
  endif()
  if(HAS_XINERAMA)
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE HAVE_XINERAMA)
    target_link_libraries("${SM_EXE_NAME}" PRIVATE X11::Xinerama)
  endif()
  if(LINUX)
    target_link_libraries("${SM_EXE_NAME}" PRIVATE usb-1.0)
  endif()
endif()

if(NOT WIN32)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE CPU_X86_64)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86|i686")
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE CPU_X86)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE CPU_AARCH64)
  endif()
endif()

# --- Game data: the macOS bundle and install() share one list ---------------

set(SM_DATA_DIRS Announcers BackgroundEffects BackgroundTransitions BGAnimations
    Characters Courses Data NoteSkins Scripts Themes)

if(APPLE)
  set(SM_BUNDLE_RESOURCES_DIR "$<TARGET_FILE_DIR:${SM_EXE_NAME}>/../Resources")
  list(TRANSFORM SM_DATA_DIRS PREPEND "${SM_ROOT_DIR}/" OUTPUT_VARIABLE SM_DATA_PATHS)

  if(NOT WITH_MACOS_PORTABLE_APP)
    # Xcode understands bundle resource directories directly. Other generators
    # (for example Unix Makefiles) end up emitting non-recursive copy commands.
    if(CMAKE_GENERATOR STREQUAL "Xcode")
      target_sources("${SM_EXE_NAME}" PRIVATE ${SM_DATA_PATHS})
      set_source_files_properties(${SM_DATA_PATHS} PROPERTIES
                                  MACOSX_PACKAGE_LOCATION Resources)
    else()
      # Enumerate resource files at configure time for dependency tracking.
      # CONFIGURE_DEPENDS re-runs cmake when files are added or removed.
      set(SM_RESOURCE_FILES "")
      set(SM_COPY_CMDS
          COMMAND "${CMAKE_COMMAND}" -E make_directory "${SM_BUNDLE_RESOURCES_DIR}")
      foreach(dir IN LISTS SM_DATA_DIRS)
        file(GLOB_RECURSE files CONFIGURE_DEPENDS "${SM_ROOT_DIR}/${dir}/*")
        list(APPEND SM_RESOURCE_FILES ${files})
        list(APPEND SM_COPY_CMDS
             COMMAND "${CMAKE_COMMAND}" -E remove_directory "${SM_BUNDLE_RESOURCES_DIR}/${dir}"
             COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SM_ROOT_DIR}/${dir}"
                     "${SM_BUNDLE_RESOURCES_DIR}/${dir}")
      endforeach()

      # Only copies when a resource is newer than the stamp file.
      set(SM_RESOURCES_STAMP "${CMAKE_CURRENT_BINARY_DIR}/resources-synced.stamp")
      add_custom_command(OUTPUT "${SM_RESOURCES_STAMP}"
                         ${SM_COPY_CMDS}
                         COMMAND "${CMAKE_COMMAND}" -E touch "${SM_RESOURCES_STAMP}"
                         DEPENDS ${SM_RESOURCE_FILES}
                         COMMENT "Syncing changed resources to bundle")
      add_custom_target(sync-resources ALL DEPENDS "${SM_RESOURCES_STAMP}")
      add_dependencies("${SM_EXE_NAME}" sync-resources)
    endif()
  else()
    message(STATUS "WITH_MACOS_PORTABLE_APP is ON: creating empty .app Resources directories and bundling only the icon")
    foreach(dir IN LISTS SM_DATA_DIRS)
      add_custom_command(TARGET "${SM_EXE_NAME}" POST_BUILD
                         COMMAND "${CMAKE_COMMAND}" -E remove_directory "${SM_BUNDLE_RESOURCES_DIR}/${dir}"
                         COMMAND "${CMAKE_COMMAND}" -E make_directory "${SM_BUNDLE_RESOURCES_DIR}/${dir}")
    endforeach()
  endif()

  # Always start from an empty Songs folder so stale songs from a prior
  # WITH_CLUB_FANTASTIC build don't persist in the bundle.
  add_custom_command(TARGET "${SM_EXE_NAME}" POST_BUILD
                     COMMAND "${CMAKE_COMMAND}" -E make_directory "${SM_BUNDLE_RESOURCES_DIR}"
                     COMMAND "${CMAKE_COMMAND}" -E remove_directory "${SM_BUNDLE_RESOURCES_DIR}/Songs"
                     COMMAND "${CMAKE_COMMAND}" -E make_directory "${SM_BUNDLE_RESOURCES_DIR}/Songs"
                     COMMAND "${CMAKE_COMMAND}" -E copy "${SM_XCODE_DIR}/logo.icns" "${SM_BUNDLE_RESOURCES_DIR}/")

  # Resources are already inside the bundle.
  install(TARGETS "${SM_EXE_NAME}" BUNDLE DESTINATION ".")
else()
  if(WIN32)
    set(SM_INSTALL_DESTINATION ".")
    set(SM_INSTALL_BIN_DESTINATION "Program")
  else()
    set(SM_INSTALL_DESTINATION "itgmania")
    set(SM_INSTALL_BIN_DESTINATION "itgmania")
  endif()

  install(TARGETS "${SM_EXE_NAME}" DESTINATION "${SM_INSTALL_BIN_DESTINATION}")
  if(WIN32)
    install(IMPORTED_RUNTIME_ARTIFACTS ffmpeg::avcodec ffmpeg::avformat
                                       ffmpeg::avutil ffmpeg::swscale
            DESTINATION "${SM_INSTALL_BIN_DESTINATION}")
    install(FILES "${SM_PROGRAM_DIR}/parallel_lights_io.dll"
            DESTINATION "${SM_INSTALL_BIN_DESTINATION}")
    if(MSVC)
      install(FILES "${SM_PROGRAM_DIR}/$<TARGET_FILE_BASE_NAME:${SM_EXE_NAME}>.vdi"
              DESTINATION "${SM_INSTALL_BIN_DESTINATION}")
    endif()
    if(TARGET TextureFontGenerator)
      install(TARGETS TextureFontGenerator DESTINATION "${SM_INSTALL_BIN_DESTINATION}")
    endif()
  elseif(LINUX)
    install(FILES "${SM_ROOT_DIR}/itgmania.desktop"
            DESTINATION "${SM_INSTALL_DESTINATION}")
    install(PROGRAMS "${SM_ROOT_DIR}/Installer/setup.sh" DESTINATION ".")
  endif()

  foreach(dir IN LISTS SM_DATA_DIRS ITEMS Docs)
    install(DIRECTORY "${SM_ROOT_DIR}/${dir}" DESTINATION "${SM_INSTALL_DESTINATION}")
  endforeach()
  install(FILES "${SM_ROOT_DIR}/Songs/instructions.txt"
          DESTINATION "${SM_INSTALL_DESTINATION}/Songs")
endif()

if(WITH_CLUB_FANTASTIC)
  include(FetchContent)
  FetchContent_Declare(
    ClubFantastic
    URL https://download.clubfantastic.dance/club_fantastic_season2_songs_9ms.zip
    URL_HASH SHA256=845b62da6bb2994fc2ba7871b35da02f6eb36ea1ae78c2b9a59a6ec046946787
  )
  FetchContent_MakeAvailable(ClubFantastic)
  FetchContent_GetProperties(ClubFantastic SOURCE_DIR CLUB_FANTASTIC_DIR)

  if(APPLE)
    # Bundle songs inside the .app so the DMG is self-contained.
    set(SM_SONGS_DESTINATION "${SM_EXE_NAME}.app/Contents/Resources/Songs")
    # Also copy into the live bundle produced by builds.
    add_custom_command(TARGET "${SM_EXE_NAME}" POST_BUILD
                       COMMAND "${CMAKE_COMMAND}" -E copy_directory
                               "${CLUB_FANTASTIC_DIR}/Club Fantastic Season 1"
                               "${SM_BUNDLE_RESOURCES_DIR}/Songs/Club Fantastic Season 1"
                       COMMAND "${CMAKE_COMMAND}" -E copy_directory
                               "${CLUB_FANTASTIC_DIR}/Club Fantastic Season 2"
                               "${SM_BUNDLE_RESOURCES_DIR}/Songs/Club Fantastic Season 2")
  else()
    set(SM_SONGS_DESTINATION "${SM_INSTALL_DESTINATION}/Songs")
  endif()
  install(DIRECTORY "${CLUB_FANTASTIC_DIR}/Club Fantastic Season 1"
                    "${CLUB_FANTASTIC_DIR}/Club Fantastic Season 2"
          DESTINATION "${SM_SONGS_DESTINATION}")
endif()
