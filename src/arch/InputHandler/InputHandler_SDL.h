#ifndef INPUT_HANDLER_SDL_H
#define INPUT_HANDLER_SDL_H

#include <atomic>
#include <map>
#include <set>
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
  // Without joysticks, SDL only reads the keyboard and mouse (see
  // InputHandler_SDLKeyboard).
  explicit InputHandler_SDL(bool bJoysticks = true);
  ~InputHandler_SDL();

  void Update();
  bool DevicesChanged() { return m_bDevicesChanged; }
  void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut);
  wchar_t DeviceButtonToChar(DeviceButton button, bool bUseCurrentKeyModifiers);

 private:
  void OpenJoystick(uint32_t id);

  bool m_bKeyboard = false;  // the SDL window is up
  bool m_bJoysticks;
  bool m_bDevicesChanged = false;
  std::set<uint32_t> m_StartupJoysticks;  // SDL_JoystickIDs, opened or not
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

/** @brief Keyboard and mouse only, for when another handler reads the
 * joysticks (LinuxEvent, say): two handlers numbering joysticks collide. */
class InputHandler_SDLKeyboard : public InputHandler_SDL {
 public:
  InputHandler_SDLKeyboard() : InputHandler_SDL(false) {}
};

#endif
