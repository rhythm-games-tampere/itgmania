#ifndef INPUT_HANDLER_SDL_H
#define INPUT_HANDLER_SDL_H

#include <atomic>
#include <map>
#include <vector>

#include "InputHandler.h"
#include "RageInputDevice.h"
#include "RageThreads.h"

struct SDL_Joystick;

/** @brief Keyboard, mouse and joysticks through SDL 3. Keyboard and mouse need
 * the SDL window (the default WindowDriver), which also makes this the
 * default in place of the platform handlers. */
class InputHandler_SDL : public InputHandler {
 public:
  InputHandler_SDL();
  ~InputHandler_SDL();

  void Update();
  bool DevicesChanged() { return m_bDevicesChanged; }
  void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut);
  wchar_t DeviceButtonToChar(DeviceButton button, bool bUseCurrentKeyModifiers);

 private:
  void OpenJoystick(uint32_t id);

  bool m_bKeyboard = false;  // the SDL window is up
  bool m_bDevicesChanged = false;
  bool m_bHadFocus = true;
  // SDL_JoystickID -> open joystick and its DEVICE_JOYn.
  struct Joystick {
    SDL_Joystick* pJoystick;
    InputDevice dev;
  };
  std::map<uint32_t, Joystick> m_Joysticks;

  // Updates joysticks every millisecond, so their events get timestamps that
  // precise instead of the frame's.
  RageThread m_JoystickThread;
  std::atomic<bool> m_bShutdown{false};
  static int JoystickThread_Start(void* p);
};

#endif
