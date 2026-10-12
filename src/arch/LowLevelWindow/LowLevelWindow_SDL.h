#ifndef LOW_LEVEL_WINDOW_SDL_H
#define LOW_LEVEL_WINDOW_SDL_H

#include <functional>
#include <string>

#include "RageDisplay.h"
#include "arch/LowLevelWindow/LowLevelWindow.h"

struct SDL_Window;
struct SDL_GLContextState;

/** @brief Runs f on the main thread and waits for it. SDL's Cocoa video only
 * works there, and on macOS the game runs on a thread of its own (SMMain.mm).
 * Elsewhere it just calls f. */
void RunOnMainThread(const std::function<void()>& f);

/** @brief Window, GL context and display modes through SDL 3. The default;
 * WindowDriver=Native in Preferences.ini selects the platform backend. */
class LowLevelWindow_SDL : public LowLevelWindow {
 public:
  LowLevelWindow_SDL();
  ~LowLevelWindow_SDL();

  void* GetProcAddress(std::string s);
  std::string TryVideoMode(const VideoModeParams& p, bool& bNewDeviceOut);
  void GetDisplaySpecs(DisplaySpecs& out) const;

  void LogDebugInformation() const;
  bool IsSoftwareRenderer(std::string& sError);

  void SwapBuffers();
  void Update();

  const ActualVideoModeParams GetActualVideoModeParams() const {
    return m_Params;
  }

  bool SupportsFullscreenBorderlessWindow() const { return true; }

  // ponytail: no threaded rendering. The X11 way (the loading thread's
  // context moves to another window) needs a second window here, and two
  // contexts on one EGL surface fail. Add it if concurrent rendering is ever
  // used again (nothing calls GameLoop::StartConcurrentRendering).

 private:
  std::string SetVideoMode(const VideoModeParams& p, bool& bNewDeviceOut);
  static void GetDisplaySpecsOnMainThread(DisplaySpecs& out);

  SDL_Window* m_pWindow = nullptr;
  SDL_GLContextState* m_Context = nullptr;
  // m_Context was made by a TryVideoMode that failed; the next one that
  // succeeds reports it as a new device.
  bool m_bNewContextPending = false;
  ActualVideoModeParams m_Params;
};

#endif
