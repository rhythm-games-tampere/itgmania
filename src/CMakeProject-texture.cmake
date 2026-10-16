set(TEXTURE_DIR "${SM_SRC_DIR}/Texture Font Generator")

add_executable("TextureFontGenerator" WIN32
               "${TEXTURE_DIR}/stdafx.cpp"
               "${TEXTURE_DIR}/Texture Font Generator.cpp"
               "${TEXTURE_DIR}/Texture Font GeneratorDlg.cpp"
               "${TEXTURE_DIR}/TextureFont.cpp"
               "${TEXTURE_DIR}/Utils.cpp"
               "${TEXTURE_DIR}/Resource.h"
               "${TEXTURE_DIR}/stdafx.h"
               "${TEXTURE_DIR}/Texture Font Generator.h"
               "${TEXTURE_DIR}/Texture Font GeneratorDlg.h"
               "${TEXTURE_DIR}/TextureFont.h"
               "${TEXTURE_DIR}/Utils.h"
               "${TEXTURE_DIR}/res/Texture Font Generator.ico"
               "${TEXTURE_DIR}/Texture Font Generator.rc"
               "${TEXTURE_DIR}/res/Texture Font Generator.rc2")

sm_vendored_target("TextureFontGenerator")
set_target_properties("TextureFontGenerator" PROPERTIES
                      FOLDER "Internal Libraries"
                      OUTPUT_NAME "Texture Font Generator"
                      RUNTIME_OUTPUT_DIRECTORY "$<1:${SM_PROGRAM_DIR}>")

# MFC as a shared library. CMAKE_MFC_FLAG only reaches Visual Studio projects,
# so other generators get the define directly.
set_property(TARGET "TextureFontGenerator" PROPERTY MSVC_RUNTIME_LIBRARY
             "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
target_compile_definitions("TextureFontGenerator" PRIVATE _AFXDLL)

# Compiled in rather than linked: the stb_image_write library uses the static
# runtime and would mix two CRTs into this /MD executable.
target_sources("TextureFontGenerator" PRIVATE "${SM_EXTERN_DIR}/stb_image_write.c")

target_include_directories("TextureFontGenerator" PRIVATE
                           "${SM_EXTERN_DIR}/stb"
                           "${TEXTURE_DIR}"
                           "${TEXTURE_DIR}/res"
                           "${SM_GENERATED_SRC_DIR}")
