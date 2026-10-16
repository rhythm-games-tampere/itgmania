# Prep options that are needed for each platform.

# This option quiets warnings that are a part of external projects.
option(WITH_EXTERNAL_WARNINGS
       "Build with warnings for all components, not just StepMania." OFF)

# Turn this on to set this to a specific release mode.
option(WITH_FULL_RELEASE "Build as a proper, full release." OFF)

# Turn this on to set this to a specific release mode.
option(WITH_NIGHTLY_RELEASE "Build as a nightly release." OFF)

# Turn this on to include Club Fantastic songs
option(WITH_CLUB_FANTASTIC "Include Club Fantastic songs." OFF)

# Turn this on for portable macOS builds that keep game resources outside the
# .app bundle.
option(WITH_MACOS_PORTABLE_APP
       "Build a macOS .app without bundling Contents/Resources game data."
       OFF)

if(MSVC)
  # Turn this option on to enable using the Texture Font Generator.
  option(
    WITH_TEXTURE_GENERATOR
    "Build with the Texture Font Generator. Ensure the MFC library is installed."
    OFF)
endif()

if(LINUX OR BSD)
  option(WITH_GTK3 "Build with GTK3 Support." ON)
  option(WITH_CRASH_HANDLER "Build with Crash Handler Support." ON)
  option(WITH_XINERAMA
         "Build using libXinerama to query for monitor numbers (if available)."
         ON)
  option(WITH_ALSA "Build with ALSA support" ON)
  option(WITH_PULSEAUDIO "Build with PulseAudio support" ON)
  option(WITH_JACK "Build with JACK support" OFF)
  option(WITH_XRANDR "Build with Xrandr support" ON)
  option(WITH_LIBXTST "Build with libXtst support" ON)
  option(WITH_X11 "Build with X11 support" ON)
endif()

