#ifndef RENDER_PROFILER_H
#define RENDER_PROFILER_H

#include <cstdint>
#include <string>

class Actor;

/** @brief Call-tree CPU profiler for actor Update/Draw/Command work.
 *
 * Active only between Start() (ScreenGameplay::BeginScreen) and Stop()
 * (leaving gameplay). Stop() writes a report to /Logs/RenderProfile-*.txt
 * and its summary to a .json next to it (Utils/RenderTest compares those).
 * RenderProfiler in Preferences.ini: 0 = off (default), 1 = full (per-actor
 * scopes, high overhead), 2 = light (frame phases, named sections and GL
 * counters only; use this for timing). */
namespace RenderProfiler {

enum Kind { kUpdate, kDraw, kCommand, kNamed };
// Named scopes that also feed the per-frame breakdown.
enum FrameSlot {
  kSlotNone = -1,
  kSlotUpdate,
  kSlotDraw,
  kSlotPresent,
  kNumSlots
};

// GL backend work, counted per category (state calls actually issued).
enum GLStat {
  kGLMatrix,
  kGLVertexSetup,
  kGLTexture,
  kGLBlend,
  kGLDepth,
  kGLCull,
  kGLAlphaTest,
  kGLLighting,
  kGLGet,  // glGet* queries: force a sync under mesa_glthread
  kGLOther,
  kNumGLStats
};

extern bool g_active;
extern bool g_actor_scopes;
extern int64_t g_gl[kNumGLStats];

inline void CountGL(GLStat s, int n = 1) {
  if (g_active) {
    g_gl[s] += n;
  }
}

void Start();
void Stop();
void FrameBegin();
void FrameEnd();

bool Push(const void* key, Kind kind, FrameSlot slot = kSlotNone);
void Pop();
void AddDraw(int verts);
const char* Intern(const std::string& s);

struct Scope {
  bool pushed;
  Scope(const void* key, Kind kind, FrameSlot slot = kSlotNone)
      : pushed(
            g_active && key != nullptr && (kind == kNamed || g_actor_scopes) &&
            Push(key, kind, slot)) {}
  ~Scope() {
    if (pushed) {
      Pop();
    }
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};

// Actor node with a child node named after the command.
struct CommandScope {
  Scope actor;
  Scope name;
  CommandScope(const Actor* a, const std::string& cmd)
      : actor(a, kCommand),
        name(actor.pushed ? Intern(cmd) : nullptr, kNamed) {}
};

// Consecutive named sections inside one function:
//   Sections s; s.Next("a"); ...; s.Next("b"); ...  (closed on scope exit)
struct Sections {
  bool pushed = false;
  void Next(const char* label) {
    if (pushed) {
      Pop();
    }
    pushed = g_active && Push(label, kNamed);
  }
  ~Sections() {
    if (pushed) {
      Pop();
    }
  }
};

}  // namespace RenderProfiler

#endif
