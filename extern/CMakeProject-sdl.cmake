# SDL 3, linked statically. Only the subsystems the game uses are built:
# video (window, GL context, display modes, events).
set(SDL_SHARED OFF CACHE INTERNAL "")
set(SDL_STATIC ON CACHE INTERNAL "")
set(SDL_STATIC_PIC ON CACHE INTERNAL "")
set(SDL_INSTALL OFF CACHE INTERNAL "")
set(SDL_TEST_LIBRARY OFF CACHE INTERNAL "")
set(SDL_TESTS OFF CACHE INTERNAL "")
set(SDL_EXAMPLES OFF CACHE INTERNAL "")
foreach(subsystem AUDIO GPU RENDER CAMERA JOYSTICK HAPTIC HIDAPI POWER SENSOR
                  DIALOG)
  set(SDL_${subsystem} OFF CACHE INTERNAL "")
endforeach()

add_subdirectory("SDL" EXCLUDE_FROM_ALL)
set_property(TARGET "SDL3-static" PROPERTY FOLDER "External Libraries")
