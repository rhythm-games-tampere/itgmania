target_sources("${SM_EXE_NAME}" PRIVATE
  "archutils/Common/HidDevice.cpp"
  "archutils/Common/HidDevice.h")

if(APPLE)
  target_sources("${SM_EXE_NAME}" PRIVATE
    "archutils/Darwin/Crash.mm"
    "archutils/Darwin/CocoaEventDispatcher.mm"
    "archutils/Darwin/DarwinThreadHelpers.cpp"
    "archutils/Darwin/HIDDevice.cpp"
    "archutils/Darwin/JoystickDevice.cpp"
    "archutils/Darwin/KeyboardDevice.cpp"
    "archutils/Darwin/MouseDevice.cpp"
    "archutils/Darwin/PumpDevice.cpp"
    "archutils/Darwin/SMMain.mm"
    "archutils/Darwin/SpecialDirs.mm"
    "archutils/Darwin/arch_setup.h"
    "archutils/Darwin/CocoaEventDispatcher.h"
    "archutils/Darwin/Crash.h"
    "archutils/Darwin/DarwinThreadHelpers.h"
    "archutils/Darwin/HIDDevice.h"
    "archutils/Darwin/JoystickDevice.h"
    "archutils/Darwin/KeyboardDevice.h"
    "archutils/Darwin/MouseDevice.h"
    "archutils/Darwin/PumpDevice.h"
    "archutils/Darwin/SpecialDirs.h"
    "archutils/Darwin/StepMania.pch")
elseif(WIN32)
  configure_file("archutils/Win32/StepMania.in.manifest"
                 "${SM_GENERATED_SRC_DIR}/archutils/Win32/StepMania.manifest")
  configure_file("archutils/Win32/WindowsResources.in.rc"
                 "${SM_GENERATED_SRC_DIR}/archutils/Win32/WindowsResources.rc")

  set(CMAKE_RC_STANDARD_INCLUDE_DIRECTORIES "${SM_SRC_DIR}/archutils/Win32")

  target_sources("${SM_EXE_NAME}" PRIVATE
    "archutils/Win32/AppInstance.cpp"
    "archutils/Win32/arch_setup.cpp"
    "archutils/Win32/arch_time.cpp"
    "archutils/Win32/CommandLine.cpp"
    "archutils/Win32/Crash.cpp"
    "archutils/Win32/CrashHandlerChild.cpp"
    "archutils/Win32/CrashHandlerNetworking.cpp"
    "archutils/Win32/DebugInfoHunt.cpp"
    "archutils/Win32/DialogUtil.cpp"
    "archutils/Win32/DirectXGuids.cpp"
    "archutils/Win32/DirectXHelpers.cpp"
    "archutils/Win32/ErrorStrings.cpp"
    "archutils/Win32/GetFileInformation.cpp"
    "archutils/Win32/GraphicsWindow.cpp"
    "archutils/Win32/MessageWindow.cpp"
    "archutils/Win32/RegistryAccess.cpp"
    "archutils/Win32/RestartProgram.cpp"
    "archutils/Win32/SpecialDirs.cpp"
    "archutils/Win32/USB.cpp"
    "archutils/Win32/VideoDriverInfo.cpp"
    "archutils/Win32/WindowIcon.cpp"
    "archutils/Win32/WindowsDialogBox.cpp"
    "${SM_GENERATED_SRC_DIR}/archutils/Win32/StepMania.manifest"
    "${SM_GENERATED_SRC_DIR}/archutils/Win32/WindowsResources.rc"
    "archutils/Win32/AppInstance.h"
    "archutils/Win32/arch_setup.h"
    "archutils/Win32/CommandLine.h"
    "archutils/Win32/Crash.h"
    "archutils/Win32/CrashHandlerInternal.h"
    "archutils/Win32/CrashHandlerNetworking.h"
    "archutils/Win32/DebugInfoHunt.h"
    "archutils/Win32/DialogUtil.h"
    "archutils/Win32/DirectXHelpers.h"
    "archutils/Win32/ErrorStrings.h"
    "archutils/Win32/GetFileInformation.h"
    "archutils/Win32/GraphicsWindow.h"
    "archutils/Win32/MessageWindow.h"
    "archutils/Win32/RegistryAccess.h"
    "archutils/Win32/RestartProgram.h"
    "archutils/Win32/SpecialDirs.h"
    "archutils/Win32/USB.h"
    "archutils/Win32/VideoDriverInfo.h"
    "archutils/Win32/WindowIcon.h"
    "archutils/Win32/WindowsDialogBox.h"
    "archutils/Win32/WindowsResources.h")
else() # Linux / BSD
  target_sources("${SM_EXE_NAME}" PRIVATE
    "archutils/Unix/AssertionHandler.cpp"
    "archutils/Unix/EmergencyShutdown.cpp"
    "archutils/Unix/GetSysInfo.cpp"
    "archutils/Unix/SpecialDirs.cpp"
    "archutils/Unix/StackCheck.cpp"
    "archutils/Common/PthreadHelpers.cpp"
    "archutils/Unix/arch_setup.h"
    "archutils/Unix/AssertionHandler.h"
    "archutils/Unix/EmergencyShutdown.h"
    "archutils/Unix/GetSysInfo.h"
    "archutils/Unix/SpecialDirs.h"
    "archutils/Common/PthreadHelpers.h")
  if(HAS_X11)
    target_sources("${SM_EXE_NAME}" PRIVATE
      "archutils/Unix/X11Helper.cpp"
      "archutils/Unix/X11Helper.h")
  endif()
endif()

if(APPLE OR LINUX)
  target_sources("${SM_EXE_NAME}" PRIVATE
    "archutils/Unix/Backtrace.cpp"
    "archutils/Unix/BacktraceNames.cpp"
    "archutils/Unix/CrashHandler.cpp"
    "archutils/Unix/CrashHandlerChild.cpp"
    "archutils/Unix/CrashHandlerInternal.cpp"
    "archutils/Unix/SignalHandler.cpp"
    "archutils/Unix/Backtrace.h"
    "archutils/Unix/BacktraceNames.h"
    "archutils/Unix/CrashHandler.h"
    "archutils/Unix/CrashHandlerInternal.h"
    "archutils/Unix/SignalHandler.h")
elseif(NOT WIN32)
  target_sources("${SM_EXE_NAME}" PRIVATE
    "archutils/Unix/SignalHandler.cpp"
    "archutils/Unix/SignalHandler.h")
endif()
