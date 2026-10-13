#ifndef RENDER_PROFILER_H
#define RENDER_PROFILER_H

#include <cstdint>
#include <string>

class Actor;
struct Message;

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
// Bumped by Start: scopes opened in an earlier session don't pop this one's.
extern uint32_t g_session;
// GLDebug level while KHR_debug is on (set by the GL renderer): 1 = a debug
// group per named section, 2 = also per actor Draw.
extern int g_debug_groups;

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
// Opens a GL debug group for the scope if g_debug_groups asks for one.
bool PushGroup(const void* key, Kind kind, FrameSlot slot);
void PopGroup();
void AddDraw(int verts);
const char* Intern(const std::string& s);

struct Scope {
  bool pushed;
  bool group;
  uint32_t session = g_session;
  Scope(const void* key, Kind kind, FrameSlot slot = kSlotNone)
      : pushed(
            g_active && key != nullptr && (kind == kNamed || g_actor_scopes) &&
            Push(key, kind, slot)),
        group(g_debug_groups != 0 && key != nullptr && PushGroup(key, kind, slot)) {}
  ~Scope() {
    if (group) {
      PopGroup();
    }
    if (pushed && session == g_session) {
      Pop();
    }
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};

// Actor node with a child node named after the command. Takes the message
// rather than its name: Message::GetName copies, which only profiling needs.
const char* InternName(const Message& msg);
struct CommandScope {
  Scope actor;
  Scope name;
  CommandScope(const Actor* a, const Message& msg)
      : actor(a, kCommand),
        name(actor.pushed ? InternName(msg) : nullptr, kNamed) {}
};

// Consecutive named sections inside one function:
//   Sections s; s.Next("a"); ...; s.Next("b"); ...  (closed on scope exit)
struct Sections {
  bool pushed = false;
  bool group = false;
  uint32_t session = 0;
  void Next(const char* label) {
    Close();
    session = g_session;
    pushed = g_active && Push(label, kNamed);
    group = g_debug_groups != 0 && PushGroup(label, kNamed, kSlotNone);
  }
  void Close() {
    if (group) {
      PopGroup();
      group = false;
    }
    if (pushed && session == g_session) {
      Pop();
    }
    pushed = false;
  }
  ~Sections() { Close(); }
};

}  // namespace RenderProfiler

#endif
