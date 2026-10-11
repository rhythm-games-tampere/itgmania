# SDL 3, linked statically. Only the subsystems the game uses are built:
# video (window, GL context, display modes, events) and joysticks.
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

# StepmaniaCore.cmake adds pthread to every check, even on Windows, where it
# makes all of SDL's link checks fail (no DirectInput or XInput, for one).
set(SM_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES}")
set(CMAKE_REQUIRED_LIBRARIES "")
add_subdirectory("SDL" EXCLUDE_FROM_ALL)
set(CMAKE_REQUIRED_LIBRARIES "${SM_REQUIRED_LIBRARIES}")
set_property(TARGET "SDL3-static" PROPERTY FOLDER "External Libraries")
