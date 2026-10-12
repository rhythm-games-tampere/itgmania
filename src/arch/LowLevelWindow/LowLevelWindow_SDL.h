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

  bool SupportsThreadedRendering() { return m_BackgroundContext != nullptr; }
  void BeginConcurrentRendering();
  void EndConcurrentRendering();

 private:
  std::string SetVideoMode(const VideoModeParams& p, bool& bNewDeviceOut);
  static void GetDisplaySpecsOnMainThread(DisplaySpecs& out);

  SDL_Window* m_pWindow = nullptr;
  SDL_GLContextState* m_Context = nullptr;
  // Shares objects with m_Context; current on the loading thread.
  SDL_GLContextState* m_BackgroundContext = nullptr;
  ActualVideoModeParams m_Params;
};

#endif
