# Built separately so GTK's include paths stay out of the main target.
add_library("LoadingWindowGtk" OBJECT
            "arch/LoadingWindow/LoadingWindow_Gtk.cpp"
            "arch/LoadingWindow/LoadingWindow_Gtk.h")

set_target_properties("LoadingWindowGtk" PROPERTIES
                      FOLDER "Internal Libraries"
                      CXX_STANDARD 17
                      CXX_STANDARD_REQUIRED ON
                      CXX_EXTENSIONS ON)

target_include_directories("LoadingWindowGtk" PRIVATE
                           "${SM_SRC_DIR}"
                           "${SM_GENERATED_SRC_DIR}"
                           "${SM_SRC_DIR}/arch/LoadingWindow")

target_link_libraries("LoadingWindowGtk" PRIVATE PkgConfig::GTK3)

target_link_libraries("${SM_EXE_NAME}" PRIVATE "LoadingWindowGtk" PkgConfig::GTK3)
target_compile_definitions("${SM_EXE_NAME}" PRIVATE HAVE_GTK)
