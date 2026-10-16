# Prebuilt ffmpeg DLLs for Windows (see .github/workflows/ffmpeg-w32.yml).
set(SM_FFMPEG_W32_DIR "${SM_EXTERN_DIR}/ffmpeg-w32")

add_library(ffmpeg INTERFACE)
target_include_directories(ffmpeg SYSTEM INTERFACE "${SM_FFMPEG_W32_DIR}/include")

foreach(lib avcodec avformat avutil swscale)
  # The DLL name carries the ABI version, e.g. avcodec-59.dll.
  file(GLOB dll "${SM_FFMPEG_W32_DIR}/x64/${lib}-*.dll")
  list(LENGTH dll dll_count)
  if(NOT dll_count EQUAL 1)
    message(FATAL_ERROR
      "Expected one ${lib}-*.dll in ${SM_FFMPEG_W32_DIR}/x64, found ${dll_count}: ${dll}")
  endif()
  add_library(ffmpeg::${lib} SHARED IMPORTED)
  set_target_properties(ffmpeg::${lib} PROPERTIES
                        IMPORTED_IMPLIB "${SM_FFMPEG_W32_DIR}/x64/${lib}.lib"
                        IMPORTED_LOCATION "${dll}")
  target_link_libraries(ffmpeg INTERFACE ffmpeg::${lib})
endforeach()
