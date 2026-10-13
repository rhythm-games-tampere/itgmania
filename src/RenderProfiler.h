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
 * counters only; use this for timing), 3 = spikes (light, plus a record of
 * every frame that missed its deadline: sections, GPU, thread CPU time, page
 * faults, Lua GC, slow actors and main-thread events such as texture loads;
 * written to /Logs/FrameSpikes-*.jsonl at Stop). */
namespace RenderProfiler {

enum Kind { kUpdate, kDraw, kCommand, kNamed };
// Named scopes that also feed the per-frame breakdown.
enum FrameSlot {
  kSlotNone = -1,
  kSlotUpdate,
  kSlotDraw,
  kSlotPresent,
  // Inside Present: where its time goes.
  kSlotLimiter,  // FrameLimitPercent's sleep
  kSlotSwap,     // SwapBuffers: vsync wait, or the driver's queue being full
  kSlotFinish,   // glFinish: the GPU catching up with the frame
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
// Time named Draw sections on the GPU too (modes 2 and 3).
extern bool g_gpu;
// Mode 3: record spikes, and time actor calls to catch slow ones.
extern bool g_spikes;
extern bool g_slow_actors;

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
// The frame's GPU timestamps (ns), indexed by RageDisplay::GpuMark's return.
void GpuResolved(const uint64_t* times, int n);
void AddDraw(int verts);
// Mode 3's slow-actor timing: a stack of open actor calls, no tree.
bool SlowBegin(const void* actor, Kind kind);
void SlowEnd();
// The actor is being destroyed; open slow-actor calls forget it.
void ActorGoneSlow(const Actor* a);
inline void ActorGone(const Actor* a) {
  if (g_slow_actors) {
    ActorGoneSlow(a);
  }
}
const char* Intern(const std::string& s);

struct Scope {
  bool pushed;
  bool slow;
  bool group;
  uint32_t session = g_session;
  Scope(const void* key, Kind kind, FrameSlot slot = kSlotNone)
      : pushed(
            g_active && key != nullptr && (kind == kNamed || g_actor_scopes) &&
            Push(key, kind, slot)),
        slow(
            g_slow_actors && kind != kNamed && key != nullptr &&
            SlowBegin(key, kind)),
        group(
            g_debug_groups != 0 && key != nullptr &&
            PushGroup(key, kind, slot)) {}
  ~Scope() {
    if (group) {
      PopGroup();
    }
    if (session == g_session) {
      if (pushed) {
        Pop();
      }
      if (slow) {
        SlowEnd();
      }
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

// Mode 3: a main-thread event during the frame (texture load, file open,
// actor load...). name must outlive the scope; set bytes if it means anything.
int64_t EventBegin();
void EventEnd(
    const char* type, const std::string& name, int64_t start, int64_t bytes);
struct EventScope {
  const char* type;
  const std::string& name;
  int64_t start;
  int64_t bytes = 0;
  uint32_t session = g_session;
  EventScope(const char* t, const std::string& n)
      : type(t), name(n), start(g_spikes ? EventBegin() : -1) {}
  ~EventScope() {
    if (start >= 0 && session == g_session) {
      EventEnd(type, name, start, bytes);
    }
  }
  EventScope(const EventScope&) = delete;
  EventScope& operator=(const EventScope&) = delete;
};

// Consecutive named sections inside one function:
//   Sections s; s.Next("a"); ...; s.Next("b"); ...  (closed on scope exit)
struct Sections {
  bool pushed = false;
  bool group = false;
  uint32_t session = 0;
  void Next(const char* label, FrameSlot slot = kSlotNone) {
    Close();
    session = g_session;
    if (label == nullptr) {
      return;
    }
    pushed = g_active && Push(label, kNamed, slot);
    group = g_debug_groups != 0 && PushGroup(label, kNamed, slot);
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
