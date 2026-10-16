add_executable("mapconv" "${SM_SRC_DIR}/archutils/Win32/mapconv.cpp")

sm_vendored_target("mapconv")
set_target_properties("mapconv" PROPERTIES
                      FOLDER "Internal Libraries"
                      RUNTIME_OUTPUT_DIRECTORY "$<1:${SM_PROGRAM_DIR}>")
