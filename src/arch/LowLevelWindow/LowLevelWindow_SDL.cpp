#include "LowLevelWindow_SDL.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <set>
#include <string>

#include "DisplaySpec.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "ProductInfo.h"
#include "RageDisplay_OGL_Helpers.h"
#include "RageException.h"
#include "RageLog.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageSurface_Load.h"
#include "RageUtil.h"
#include "RageUtil/Endian.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "global.h"

#if defined(_WIN32)
#define SDL_MAIN_HANDLED  // only for SDL_RegisterApp; main stays ours
#include <SDL3/SDL_main.h>

#include "archutils/Win32/GraphicsWindow.h"
#elif defined(MACOSX)
#include <dispatch/dispatch.h>
#include <pthread.h>

void MacOSX_StopAppEventLoop();  // archutils/Darwin/SMMain.mm
#endif

// Off asks for a compatibility context, like the platform windows.
static Preference<bool> g_bOpenGLCoreProfile("OpenGLCoreProfile", true);

#if defined(__linux__)
// SDL video drivers to try in order; "" lets SDL pick, which prefers
// Wayland. X11 first, falling back to Wayland where there is no X server.
static Preference<std::string> g_sSDLVideoDriver(
    "SDLVideoDriver", "x11,wayland");
#endif

// Core 3.3; macOS gives 4.1 for any core request, and only forward
// compatible ones.
static void SetCoreProfileAttributes(bool bCore) {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, bCore ? 3 : 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, bCore ? 3 : 1);
  SDL_GL_SetAttribute(
      SDL_GL_CONTEXT_PROFILE_MASK, bCore ? SDL_GL_CONTEXT_PROFILE_CORE : 0);
#if defined(MACOSX)
  SDL_GL_SetAttribute(
      SDL_GL_CONTEXT_FLAGS, bCore ? SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG : 0);
#endif
}

void RunOnMainThread(const std::function<void()>& f) {
#if defined(MACOSX)
  if (pthread_main_np() == 0) {
    dispatch_sync_f(
        dispatch_get_main_queue(), const_cast<std::function<void()>*>(&f),
        [](void* p) { (*static_cast<std::function<void()>*>(p))(); });
    return;
  }
#endif
  f();
}

// DisplaySpec ids are SDL display ids, which are only stable within a run, so
// the first display with a matching name is used too (ids from another
// backend, like "\\.\DISPLAY1", match neither and pick the primary display).
static SDL_DisplayID FindDisplay(const std::string& sId) {
  int iCount = 0;
  SDL_DisplayID* pDisplays = SDL_GetDisplays(&iCount);
  SDL_DisplayID found = 0;
  for (int i = 0; i < iCount && found == 0; ++i) {
    const char* sName = SDL_GetDisplayName(pDisplays[i]);
    if (sId == ssprintf("%u", pDisplays[i]) ||
        (sName != nullptr && sId == sName)) {
      found = pDisplays[i];
    }
  }
  SDL_free(pDisplays);
  return found != 0 ? found : SDL_GetPrimaryDisplay();
}

static int RefreshRate(const SDL_DisplayMode* pMode) {
  // 0 means unknown.
  return pMode != nullptr && pMode->refresh_rate > 0
             ? static_cast<int>(std::lround(pMode->refresh_rate))
             : 60;
}

static void SetWindowIcon(SDL_Window* pWindow, const std::string& sIconFile) {
  if (sIconFile.empty()) {
    return;
  }
  std::string sError;
  RageSurface* pImg = RageSurfaceUtils::LoadFile(sIconFile, sError);
  if (pImg == nullptr) {
    LOG->Warn(
        "Couldn't open icon \"%s\": %s", sIconFile.c_str(), sError.c_str());
    return;
  }
  // Bytes R, G, B, A in memory, which is SDL_PIXELFORMAT_RGBA32.
  RageSurface* pRGBA = CreateSurface(
      pImg->w, pImg->h, 32, Swap32BE(0xFF000000), Swap32BE(0x00FF0000),
      Swap32BE(0x0000FF00), Swap32BE(0x000000FF));
  RageSurfaceUtils::Blit(pImg, pRGBA);
  SDL_Surface* pIcon = SDL_CreateSurfaceFrom(
      pRGBA->w, pRGBA->h, SDL_PIXELFORMAT_RGBA32, pRGBA->pixels, pRGBA->pitch);
  if (pIcon != nullptr) {
    SDL_SetWindowIcon(pWindow, pIcon);
    SDL_DestroySurface(pIcon);
  }
  delete pRGBA;
  delete pImg;
}

LowLevelWindow_SDL::LowLevelWindow_SDL() {
#if defined(_WIN32)
  // The window class name ArchHooks_Win32 looks for to hand a second
  // instance's command line to this one.
  SDL_RegisterApp(PRODUCT_ID, 0, nullptr);
#elif defined(__linux__)
  if (!g_sSDLVideoDriver.Get().empty()) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, g_sSDLVideoDriver.Get().c_str());
  }
#endif
  std::string sError;
  RunOnMainThread([&] {
#if defined(MACOSX)
    MacOSX_StopAppEventLoop();
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      sError = SDL_GetError();
    }
  });
  if (!sError.empty()) {
    RageException::Throw("SDL_Init failed: %s", sError.c_str());
  }
  // Off until InputHandler_SDL takes them.
  for (Uint32 type :
       {SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP, SDL_EVENT_MOUSE_MOTION,
        SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP,
        SDL_EVENT_MOUSE_WHEEL}) {
    SDL_SetEventEnabled(type, false);
  }
}

LowLevelWindow_SDL::~LowLevelWindow_SDL() {
#if defined(_WIN32)
  GraphicsWindow::AdoptWindow(nullptr);
#endif
  if (m_Context != nullptr) {
    SDL_GL_MakeCurrent(m_pWindow, nullptr);
  }
  RunOnMainThread([this] {
    if (m_Context != nullptr) {
      SDL_GL_DestroyContext(m_Context);
    }
    if (m_pWindow != nullptr) {
      SDL_DestroyWindow(m_pWindow);
    }
    SDL_Quit();
  });
}

void* LowLevelWindow_SDL::GetProcAddress(std::string s) {
  return reinterpret_cast<void*>(SDL_GL_GetProcAddress(s.c_str()));
}

std::string LowLevelWindow_SDL::TryVideoMode(
    const VideoModeParams& p, bool& bNewDeviceOut) {
  std::string sError;
  bool bNewContext = false;
  RunOnMainThread([&] { sError = SetVideoMode(p, bNewContext); });
  if (bNewContext) {
    // Rendering happens on this thread, which isn't the one the contexts were
    // made on with macOS.
    if (!SDL_GL_MakeCurrent(m_pWindow, m_Context)) {
      bNewDeviceOut = false;
      return ssprintf("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
    }
    m_bNewContextPending = true;
  }
  // A failed mode can still have made the context; RageDisplay only sets
  // the new one up after a mode that works, so report it then.
  bNewDeviceOut = sError.empty() && m_bNewContextPending;
  if (!sError.empty()) {
    return sError;
  }
  m_bNewContextPending = false;
  if (!SDL_GL_SetSwapInterval(p.vsync ? 1 : 0)) {
    LOG->Warn("SDL_GL_SetSwapInterval failed: %s", SDL_GetError());
  }
  return std::string();
}

// The window side of TryVideoMode, on the main thread.
std::string LowLevelWindow_SDL::SetVideoMode(
    const VideoModeParams& p, bool& bNewDeviceOut) {
  bNewDeviceOut = false;

  if (m_pWindow == nullptr) {
    // The pixel format is picked when the window is made, so bpp only takes
    // effect then.
    const int iColorBits = p.bpp == 16 ? 5 : 8;
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, iColorBits);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, p.bpp == 16 ? 6 : 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, iColorBits);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SetCoreProfileAttributes(g_bOpenGLCoreProfile.Get());

    m_pWindow = SDL_CreateWindow(
        p.sWindowTitle.c_str(), p.width, p.height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (m_pWindow == nullptr) {
      return ssprintf("SDL_CreateWindow failed: %s", SDL_GetError());
    }

    m_Context = SDL_GL_CreateContext(m_pWindow);
    if (m_Context == nullptr && g_bOpenGLCoreProfile.Get()) {
      // Drivers without GL 3.2 have no core profile.
      LOG->Warn(
          "No OpenGL core profile context (%s); trying compatibility.",
          SDL_GetError());
      SetCoreProfileAttributes(false);
      m_Context = SDL_GL_CreateContext(m_pWindow);
    }
    if (m_Context == nullptr) {
      return ssprintf("SDL_GL_CreateContext failed: %s", SDL_GetError());
    }
    // Creating a context makes it current here; TryVideoMode makes m_Context
    // current on the rendering thread.
    SDL_GL_MakeCurrent(m_pWindow, nullptr);
#if defined(_WIN32)
    GraphicsWindow::AdoptWindow(
        static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(m_pWindow),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)));
#endif
    bNewDeviceOut = true;
  }

  SDL_SetWindowTitle(m_pWindow, p.sWindowTitle.c_str());
  SetWindowIcon(m_pWindow, p.sIconFile);

  const SDL_DisplayID display = FindDisplay(p.sDisplayId);
  const SDL_DisplayMode* pDesktop = SDL_GetDesktopDisplayMode(display);
  ActualVideoModeParams params(p);
  params.rate = RefreshRate(pDesktop);

  if (p.windowed && !p.bWindowIsFullscreenBorderless) {
    SDL_SetWindowFullscreen(m_pWindow, false);
    SDL_SetWindowBordered(m_pWindow, true);
    SDL_SetWindowResizable(m_pWindow, true);
    SDL_SetWindowSize(m_pWindow, p.width, p.height);
    // Like LowLevelWindow_Win32: centered on the primary display.
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    SDL_SetWindowPosition(
        m_pWindow, SDL_WINDOWPOS_CENTERED_DISPLAY(primary),
        SDL_WINDOWPOS_CENTERED_DISPLAY(primary));
  } else {
    // Move onto the display first; fullscreen uses the one the window is on.
    SDL_SetWindowPosition(
        m_pWindow, SDL_WINDOWPOS_CENTERED_DISPLAY(display),
        SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    if (p.bWindowIsFullscreenBorderless) {
      // Desktop-sized; RageDisplay renders offscreen and scales if the
      // requested resolution differs (like LowLevelWindow_X11).
      SDL_SetWindowFullscreenMode(m_pWindow, nullptr);
    } else {
      SDL_DisplayMode mode;
      if (!SDL_GetClosestFullscreenDisplayMode(
              display, p.width, p.height, static_cast<float>(p.rate), false,
              &mode)) {
        return ssprintf(
            "Couldn't set screen mode %dx%d: %s", p.width, p.height,
            SDL_GetError());
      }
      if (!SDL_SetWindowFullscreenMode(m_pWindow, &mode)) {
        return ssprintf("Couldn't set screen mode: %s", SDL_GetError());
      }
      params.width = mode.w;
      params.height = mode.h;
      params.rate = RefreshRate(&mode);
    }
    if (!SDL_SetWindowFullscreen(m_pWindow, true)) {
      return ssprintf("Couldn't go fullscreen: %s", SDL_GetError());
    }
  }
  SDL_ShowWindow(m_pWindow);
  SDL_RaiseWindow(m_pWindow);
  SDL_SyncWindow(m_pWindow);

  SDL_GetWindowSizeInPixels(
      m_pWindow, &params.windowWidth, &params.windowHeight);
  if (p.windowed && !p.bWindowIsFullscreenBorderless) {
    // The window manager may not have given us the size we asked for.
    params.width = params.windowWidth;
    params.height = params.windowHeight;
  }
  params.renderOffscreen = params.windowWidth != params.width ||
                           params.windowHeight != params.height;
  m_Params = params;
  return std::string();
}

void LowLevelWindow_SDL::GetDisplaySpecs(DisplaySpecs& out) const {
  RunOnMainThread([&] { GetDisplaySpecsOnMainThread(out); });
}

void LowLevelWindow_SDL::GetDisplaySpecsOnMainThread(DisplaySpecs& out) {
  int iCount = 0;
  SDL_DisplayID* pDisplays = SDL_GetDisplays(&iCount);
  for (int i = 0; i < iCount; ++i) {
    const SDL_DisplayID id = pDisplays[i];
    auto mode = [](const SDL_DisplayMode* m) {
      return DisplayMode{
          static_cast<unsigned>(m->w), static_cast<unsigned>(m->h),
          static_cast<double>(m->refresh_rate)};
    };
    std::set<DisplayMode> modes;
    int iModes = 0;
    SDL_DisplayMode** pModes = SDL_GetFullscreenDisplayModes(id, &iModes);
    for (int j = 0; j < iModes; ++j) {
      modes.insert(mode(pModes[j]));
    }
    SDL_free(pModes);

    const char* sName = SDL_GetDisplayName(id);
    const std::string sId = ssprintf("%u", id);
    const SDL_DisplayMode* pDesktop = SDL_GetDesktopDisplayMode(id);
    SDL_Rect r;
    if (pDesktop != nullptr && SDL_GetDisplayBounds(id, &r)) {
      const DisplayMode cur = mode(pDesktop);
      modes.insert(cur);  // DisplaySpec requires it in the list
      out.insert(DisplaySpec(
          sId, sName ? sName : sId, modes, cur, RectI(r.x, r.y, r.w, r.h)));
    } else if (!modes.empty()) {
      out.insert(DisplaySpec(sId, sName ? sName : sId, modes));
    }
  }
  SDL_free(pDisplays);
}

void LowLevelWindow_SDL::LogDebugInformation() const {
  const int v = SDL_GetVersion();
  int iProfile = 0;
  SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &iProfile);
  LOG->Info(
      "SDL %d.%d.%d, video driver %s, OpenGL %s profile",
      SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v),
      SDL_GetCurrentVideoDriver(),
      iProfile == SDL_GL_CONTEXT_PROFILE_CORE ? "core" : "compatibility");
}

bool LowLevelWindow_SDL::IsSoftwareRenderer(std::string& sError) {
  const std::string sVendor =
      reinterpret_cast<const char*>(glGetString(GL_VENDOR));
  const std::string sRenderer =
      reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  LOG->Trace(
      "LowLevelWindow_SDL::IsSoftwareRenderer '%s', '%s'", sVendor.c_str(),
      sRenderer.c_str());
  for (const char* s :
       {"GDI Generic", "llvmpipe", "softpipe", "Software Rasterizer",
        "Apple Software Renderer"}) {
    if (sRenderer.find(s) != std::string::npos) {
      sError = "OpenGL hardware acceleration is not available.";
      return true;
    }
  }
  return false;
}

void LowLevelWindow_SDL::SwapBuffers() { SDL_GL_SwapWindow(m_pWindow); }

void LowLevelWindow_SDL::Update() {
  const bool bShowCursor = PREFSMAN->m_bShowMouseCursor;
  RunOnMainThread([bShowCursor] {
    if (bShowCursor) {
      SDL_ShowCursor();
    } else {
      SDL_HideCursor();
    }
    SDL_PumpEvents();
  });

  // Quit, display and window events are ours; keyboard through joystick
  // events are InputHandler_SDL's. Drop the rest, and input events too if no
  // InputHandler_SDL turned them on.
  bool bResized = false;
  SDL_FlushEvents(SDL_EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_LAST);
  if (!SDL_EventEnabled(SDL_EVENT_KEY_DOWN)) {
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_GAMEPAD_AXIS_MOTION - 1);
  }
  SDL_Event e;
  while (SDL_PeepEvents(
             &e, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_KEY_DOWN - 1) >
         0) {
    switch (e.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        ArchHooks::SetUserQuit();
        break;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        HOOKS->SetHasFocus(true);
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        HOOKS->SetHasFocus(false);
        break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        bResized = true;
        break;
      default:
        break;
    }
  }

  // A resized window renders at its new size (fullscreen sizes are fixed).
  if (bResized && m_Params.windowed &&
      !m_Params.bWindowIsFullscreenBorderless) {
    int w = 0, h = 0;
    RunOnMainThread([&] { SDL_GetWindowSizeInPixels(m_pWindow, &w, &h); });
    if (w != m_Params.width || h != m_Params.height) {
      m_Params.width = m_Params.windowWidth = w;
      m_Params.height = m_Params.windowHeight = h;
      if (DISPLAY != nullptr) {
        DISPLAY->ResolutionChanged();
      }
    }
  }
}
