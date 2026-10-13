#include "InputHandler_SDL.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <string>
#include <vector>

#include "InputFilter.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "arch/LowLevelWindow/LowLevelWindow_SDL.h"
#include "global.h"

REGISTER_INPUT_HANDLER_CLASS(SDL);
REGISTER_INPUT_HANDLER_CLASS(SDLKeyboard);

// Physical key positions, like InputHandler_DirectInput's scancode table, so
// keyboard mappings carry over.
static DeviceButton ScancodeToDeviceButton(SDL_Scancode sc) {
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
    return enum_add2(KEY_Ca, sc - SDL_SCANCODE_A);
  }
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
    return enum_add2(KEY_C1, sc - SDL_SCANCODE_1);
  }
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) {
    return enum_add2(KEY_F1, sc - SDL_SCANCODE_F1);
  }
  if (sc >= SDL_SCANCODE_F13 && sc <= SDL_SCANCODE_F16) {
    return enum_add2(KEY_F13, sc - SDL_SCANCODE_F13);
  }
  if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) {
    return enum_add2(KEY_KP_C1, sc - SDL_SCANCODE_KP_1);
  }
  switch (sc) {
    case SDL_SCANCODE_0:
      return KEY_C0;
    case SDL_SCANCODE_RETURN:
      return KEY_ENTER;
    case SDL_SCANCODE_ESCAPE:
      return KEY_ESC;
    case SDL_SCANCODE_BACKSPACE:
      return KEY_BACK;
    case SDL_SCANCODE_TAB:
      return KEY_TAB;
    case SDL_SCANCODE_SPACE:
      return KEY_SPACE;
    case SDL_SCANCODE_MINUS:
      return KEY_HYPHEN;
    case SDL_SCANCODE_EQUALS:
      return KEY_EQUAL;
    case SDL_SCANCODE_LEFTBRACKET:
      return KEY_LBRACKET;
    case SDL_SCANCODE_RIGHTBRACKET:
      return KEY_RBRACKET;
    case SDL_SCANCODE_BACKSLASH:
      return KEY_BACKSLASH;
    case SDL_SCANCODE_NONUSBACKSLASH:
      return KEY_BACKSLASH;  // DIK_OEM_102
    case SDL_SCANCODE_SEMICOLON:
      return KEY_SEMICOLON;
    case SDL_SCANCODE_APOSTROPHE:
      return KEY_SQUOTE;
    case SDL_SCANCODE_GRAVE:
      return KEY_ACCENT;
    case SDL_SCANCODE_COMMA:
      return KEY_COMMA;
    case SDL_SCANCODE_PERIOD:
      return KEY_PERIOD;
    case SDL_SCANCODE_SLASH:
      return KEY_SLASH;
    case SDL_SCANCODE_CAPSLOCK:
      return KEY_CAPSLOCK;
    case SDL_SCANCODE_PRINTSCREEN:
      return KEY_PRTSC;
    case SDL_SCANCODE_SCROLLLOCK:
      return KEY_SCRLLOCK;
    case SDL_SCANCODE_PAUSE:
      return KEY_PAUSE;
    case SDL_SCANCODE_INSERT:
      return KEY_INSERT;
    case SDL_SCANCODE_HOME:
      return KEY_HOME;
    case SDL_SCANCODE_PAGEUP:
      return KEY_PGUP;
    case SDL_SCANCODE_DELETE:
      return KEY_DEL;
    case SDL_SCANCODE_END:
      return KEY_END;
    case SDL_SCANCODE_PAGEDOWN:
      return KEY_PGDN;
    case SDL_SCANCODE_RIGHT:
      return KEY_RIGHT;
    case SDL_SCANCODE_LEFT:
      return KEY_LEFT;
    case SDL_SCANCODE_DOWN:
      return KEY_DOWN;
    case SDL_SCANCODE_UP:
      return KEY_UP;
    case SDL_SCANCODE_NUMLOCKCLEAR:
      return KEY_NUMLOCK;
    case SDL_SCANCODE_KP_DIVIDE:
      return KEY_KP_SLASH;
    case SDL_SCANCODE_KP_MULTIPLY:
      return KEY_KP_ASTERISK;
    case SDL_SCANCODE_KP_MINUS:
      return KEY_KP_HYPHEN;
    case SDL_SCANCODE_KP_PLUS:
      return KEY_KP_PLUS;
    case SDL_SCANCODE_KP_ENTER:
      return KEY_KP_ENTER;
    case SDL_SCANCODE_KP_0:
      return KEY_KP_C0;
    case SDL_SCANCODE_KP_PERIOD:
      return KEY_KP_PERIOD;
    case SDL_SCANCODE_KP_EQUALS:
      return KEY_KP_EQUAL;
    case SDL_SCANCODE_APPLICATION:
      return KEY_MENU;
    case SDL_SCANCODE_LCTRL:
      return KEY_LCTRL;
    case SDL_SCANCODE_LSHIFT:
      return KEY_LSHIFT;
    case SDL_SCANCODE_LALT:
      return KEY_LALT;
    case SDL_SCANCODE_LGUI:
      return KEY_LMETA;
    case SDL_SCANCODE_RCTRL:
      return KEY_RCTRL;
    case SDL_SCANCODE_RSHIFT:
      return KEY_RSHIFT;
    case SDL_SCANCODE_RALT:
      return KEY_RALT;
    case SDL_SCANCODE_RGUI:
      return KEY_RMETA;
    default:
      return DeviceButton_Invalid;
  }
}

// SDL timestamps count from SDL_GetTicksNS; RageTimer has its own clock.
static RageTimer ToRageTimer(Uint64 timestamp) {
  RageTimer now;
  const Uint64 sdl_now = SDL_GetTicksNS();
  if (timestamp == 0 || timestamp > sdl_now) {
    return now;
  }
  return now - static_cast<float>(sdl_now - timestamp) / SDL_NS_PER_SECOND;
}

// Joystick axes in DirectInput's order.
static const DeviceButton kAxes[][2] = {
    {JOY_LEFT, JOY_RIGHT},      {JOY_UP, JOY_DOWN},
    {JOY_Z_UP, JOY_Z_DOWN},     {JOY_ROT_LEFT, JOY_ROT_RIGHT},
    {JOY_ROT_UP, JOY_ROT_DOWN}, {JOY_ROT_Z_UP, JOY_ROT_Z_DOWN},
    {JOY_AUX_1, JOY_AUX_2},     {JOY_AUX_3, JOY_AUX_4},
};

InputHandler_SDL::InputHandler_SDL(bool bJoysticks) : m_bJoysticks(bJoysticks) {
  m_bKeyboard = SDL_WasInit(SDL_INIT_VIDEO) != 0;
  // Keys from SDL's raw input thread, timestamped as they arrive; window
  // messages only carry GetMessageTime's ~16 ms ticks.
  SDL_SetHint(SDL_HINT_WINDOWS_RAW_KEYBOARD, "1");
  SDL_SetHint(SDL_HINT_AUTO_UPDATE_JOYSTICKS, "0");
  // Pads keep working while another window has focus, like DirectInput's
  // DISCL_BACKGROUND and the Linux and macOS handlers.
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (m_bJoysticks && !SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
    LOG->Warn("SDL joystick init failed: %s", SDL_GetError());
    m_bJoysticks = false;
  }
  for (Uint32 type :
       {SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP, SDL_EVENT_MOUSE_MOTION,
        SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP,
        SDL_EVENT_MOUSE_WHEEL}) {
    SDL_SetEventEnabled(type, m_bKeyboard);
  }

  // Numbered (DEVICE_JOY1...) in device path order, which stays the same
  // across runs and replugging into the same ports; SDL's own order doesn't.
  if (!m_bJoysticks) {
    return;
  }
  int iCount = 0;
  SDL_JoystickID* pIds = SDL_GetJoysticks(&iCount);
  std::vector<std::pair<std::string, SDL_JoystickID>> vSorted;
  for (int i = 0; i < iCount; ++i) {
    m_StartupJoysticks.insert(pIds[i]);
    const char* sPath = SDL_GetJoystickPathForID(pIds[i]);
    vSorted.emplace_back(sPath != nullptr ? sPath : "", pIds[i]);
  }
  SDL_free(pIds);
  std::stable_sort(vSorted.begin(), vSorted.end());
  for (const auto& j : vSorted) {
    OpenJoystick(j.second);
  }

  m_JoystickThread.SetName("SDL joystick thread");
  m_JoystickThread.Create(JoystickThread_Start, this);
}

InputHandler_SDL::~InputHandler_SDL() {
  if (m_JoystickThread.IsCreated()) {
    m_bShutdown = true;
    m_JoystickThread.Wait();
  }
  for (auto& j : m_Joysticks) {
    SDL_CloseJoystick(j.second.pJoystick);
  }
  for (Uint32 type :
       {SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP, SDL_EVENT_MOUSE_MOTION,
        SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP,
        SDL_EVENT_MOUSE_WHEEL}) {
    SDL_SetEventEnabled(type, false);
  }
  if (m_bJoysticks) {
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
  }
  SDL_ResetHint(SDL_HINT_AUTO_UPDATE_JOYSTICKS);
  SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
}

int InputHandler_SDL::JoystickThread_Start(void* p) {
  InputHandler_SDL* self = static_cast<InputHandler_SDL*>(p);
  SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
  // Without open joysticks this only notices new ones, which can wait. The
  // set never changes: hotplugging recreates the handler.
  const Uint64 iDelay =
      self->m_Joysticks.empty() ? 16 * SDL_NS_PER_MS : SDL_NS_PER_MS;
  while (!self->m_bShutdown) {
    SDL_UpdateJoysticks();
    SDL_DelayNS(iDelay);
  }
  return 0;
}

void InputHandler_SDL::OpenJoystick(uint32_t id) {
  if (m_Joysticks.count(id) || m_Joysticks.size() >= NUM_JOYSTICKS) {
    return;
  }
  SDL_Joystick* pJoystick = SDL_OpenJoystick(id);
  if (pJoystick == nullptr) {
    LOG->Warn("Couldn't open joystick %u: %s", id, SDL_GetError());
    return;
  }
  const InputDevice dev = enum_add2(DEVICE_JOY1, int(m_Joysticks.size()));
  m_Joysticks[id] = {pJoystick, dev};
  LOG->Info(
      "SDL joystick %d: %s (%d axes, %d buttons, %d hats)",
      dev - DEVICE_JOY1 + 1, SDL_GetJoystickName(pJoystick),
      SDL_GetNumJoystickAxes(pJoystick), SDL_GetNumJoystickButtons(pJoystick),
      SDL_GetNumJoystickHats(pJoystick));
}

void InputHandler_SDL::Update() {
#if defined(MACOSX)
  if (m_bJoysticks) {
    // The joystick thread reads values (IOHIDDeviceGetValue works on any
    // thread), but IOKit's hotplug callbacks only run in the run loop of the
    // thread that started the joystick subsystem: this one.
    SDL_UpdateJoysticks();
  }
#endif
  PumpSDLEvents();
  SDL_Event e;
  while (SDL_PeepEvents(
             &e, 1, SDL_GETEVENT, SDL_EVENT_KEY_DOWN,
             SDL_EVENT_GAMEPAD_AXIS_MOTION - 1) > 0) {
    const RageTimer tm = ToRageTimer(e.common.timestamp);
    switch (e.type) {
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP: {
        if (e.key.repeat) {
          break;  // InputFilter makes its own repeats
        }
        const DeviceButton b = ScancodeToDeviceButton(e.key.scancode);
        if (b != DeviceButton_Invalid) {
          ButtonPressed(DeviceInput(DEVICE_KEYBOARD, b, e.key.down, tm));
        }
        break;
      }
      case SDL_EVENT_MOUSE_MOTION: {
        // In the game's resolution, like the platform handlers' window
        // pixels: the window can differ from it (rendering offscreen at
        // another resolution, or points on a high-density display).
        float x = e.motion.x, y = e.motion.y;
        int w = 0, h = 0;
        SDL_Window* pWindow = SDL_GetWindowFromID(e.motion.windowID);
        if (DISPLAY != nullptr && pWindow != nullptr &&
            SDL_GetWindowSize(pWindow, &w, &h) && w > 0 && h > 0) {
          const ActualVideoModeParams params =
              DISPLAY->GetActualVideoModeParams();
          x *= static_cast<float>(params.width) / w;
          y *= static_cast<float>(params.height) / h;
        }
        INPUTFILTER->UpdateCursorLocation(x, y);
        break;
      }
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        DeviceButton b = DeviceButton_Invalid;
        switch (e.button.button) {
          case SDL_BUTTON_LEFT:
            b = MOUSE_LEFT;
            break;
          case SDL_BUTTON_RIGHT:
            b = MOUSE_RIGHT;
            break;
          case SDL_BUTTON_MIDDLE:
            b = MOUSE_MIDDLE;
            break;
          default:
            break;
        }
        if (b != DeviceButton_Invalid) {
          ButtonPressed(DeviceInput(DEVICE_MOUSE, b, e.button.down, tm));
        }
        break;
      }
      case SDL_EVENT_MOUSE_WHEEL: {
        // Undo "natural" scrolling (macOS), so up is always up.
        const int iFlip = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
        // In Windows' units (WHEEL_DELTA per notch), like DirectInput.
        INPUTFILTER->UpdateMouseWheel(e.wheel.y * iFlip * 120);
        // Whole notches only: trackpads scroll in many fractional steps.
        const int iNotches = e.wheel.integer_y * iFlip;
        if (iNotches != 0) {
          const DeviceButton b = iNotches > 0 ? MOUSE_WHEELUP : MOUSE_WHEELDOWN;
          ButtonPressed(DeviceInput(DEVICE_MOUSE, b, 1, tm));
          ButtonPressed(DeviceInput(DEVICE_MOUSE, b, 0, tm));
        }
        break;
      }
      case SDL_EVENT_JOYSTICK_ADDED:
        // Not ones that were there at startup but couldn't be opened (or
        // were over NUM_JOYSTICKS): recreating would find them again.
        if (!m_StartupJoysticks.count(e.jdevice.which)) {
          m_bDevicesChanged = true;  // RageInput recreates the handlers
        }
        break;
      case SDL_EVENT_JOYSTICK_REMOVED:
        if (m_Joysticks.count(e.jdevice.which)) {
          m_bDevicesChanged = true;
        }
        break;
      case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
      case SDL_EVENT_JOYSTICK_BUTTON_UP: {
        auto it = m_Joysticks.find(e.jbutton.which);
        if (it != m_Joysticks.end() && e.jbutton.button < 32) {
          ButtonPressed(DeviceInput(
              it->second.dev, enum_add2(JOY_BUTTON_1, e.jbutton.button),
              e.jbutton.down, tm));
        }
        break;
      }
      case SDL_EVENT_JOYSTICK_AXIS_MOTION: {
        auto it = m_Joysticks.find(e.jaxis.which);
        if (it != m_Joysticks.end() && e.jaxis.axis < ARRAYLEN(kAxes)) {
          const float l = std::max(e.jaxis.value / 32767.0f, -1.0f);
          ButtonPressed(DeviceInput(
              it->second.dev, kAxes[e.jaxis.axis][0], std::max(-l, 0.0f), tm));
          ButtonPressed(DeviceInput(
              it->second.dev, kAxes[e.jaxis.axis][1], std::max(l, 0.0f), tm));
        }
        break;
      }
      case SDL_EVENT_JOYSTICK_HAT_MOTION: {
        auto it = m_Joysticks.find(e.jhat.which);
        if (it != m_Joysticks.end() && e.jhat.hat == 0) {
          const InputDevice dev = it->second.dev;
          const Uint8 v = e.jhat.value;
          ButtonPressed(DeviceInput(dev, JOY_HAT_UP, !!(v & SDL_HAT_UP), tm));
          ButtonPressed(
              DeviceInput(dev, JOY_HAT_DOWN, !!(v & SDL_HAT_DOWN), tm));
          ButtonPressed(
              DeviceInput(dev, JOY_HAT_LEFT, !!(v & SDL_HAT_LEFT), tm));
          ButtonPressed(
              DeviceInput(dev, JOY_HAT_RIGHT, !!(v & SDL_HAT_RIGHT), tm));
        }
        break;
      }
      default:
        break;
    }
  }
  InputHandler::UpdateTimer();
}

void InputHandler_SDL::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& vDevicesOut) {
  if (m_bKeyboard) {
    vDevicesOut.push_back(InputDeviceInfo(DEVICE_KEYBOARD, "Keyboard"));
    vDevicesOut.push_back(InputDeviceInfo(DEVICE_MOUSE, "Mouse"));
  }
  for (const auto& j : m_Joysticks) {
    const char* sName = SDL_GetJoystickName(j.second.pJoystick);
    vDevicesOut.push_back(InputDeviceInfo(j.second.dev, sName ? sName : ""));
  }
}

wchar_t InputHandler_SDL::DeviceButtonToChar(
    DeviceButton button, bool bUseCurrentKeyModifiers) {
  for (int sc = SDL_SCANCODE_UNKNOWN; sc < SDL_SCANCODE_COUNT; ++sc) {
    if (ScancodeToDeviceButton(static_cast<SDL_Scancode>(sc)) != button) {
      continue;
    }
    // The current layout's character for that key.
    const SDL_Keycode key = SDL_GetKeyFromScancode(
        static_cast<SDL_Scancode>(sc),
        bUseCurrentKeyModifiers ? SDL_GetModState() : SDL_KMOD_NONE, false);
    // Keycodes of printable keys are their code points.
    if (key >= 32 && key != SDLK_DELETE && !(key & SDLK_SCANCODE_MASK)) {
      return static_cast<wchar_t>(key);
    }
    break;
  }
  return InputHandler::DeviceButtonToChar(button, bUseCurrentKeyModifiers);
}
