#include "RenderProfiler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <thread>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <intrin.h>
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <time.h>
#if defined(__x86_64__)
#include <x86intrin.h>
#endif
#endif

#include "Actor.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "GameState.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "Preference.h"
#include "RageDisplay.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "Song.h"
#include "SongOptions.h"
#include "Steps.h"

namespace RenderProfiler {

bool g_active = false;
bool g_actor_scopes = false;
int64_t g_gl[kNumGLStats];
uint32_t g_session = 0;
int g_debug_groups = 0;
bool g_gpu = false;
bool g_spikes = false;
bool g_slow_actors = false;

namespace {

Preference<int> g_iMode("RenderProfiler", 0);
// Mode 3: an actor Update/Draw/command call this slow (exclusive of its
// children) is listed in a spike record.
Preference<float> g_fSlowActorMs("RenderProfilerSlowActorMs", 0.25f);
// Mode 3: a frame slower than this is a spike. 0: 1.5 refresh intervals with
// vsync, else twice the recent median (and at least 4 ms over it).
Preference<float> g_fSpikeMs("RenderProfilerSpikeMs", 0);

using Clock = std::chrono::steady_clock;

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch())
      .count();
}

struct Node {
  int parent;
  const void* key;
  Kind kind;
  std::string label;
  std::string path;  // static actor lineage, for actor nodes
  int64_t incl_ns = 0;
  int64_t child_ns = 0;
  int64_t gpu_ns = 0;
  int64_t calls = 0;
  int64_t draws = 0;
  int64_t verts = 0;
  std::unordered_map<uint64_t, int> children;
};

struct Frame {
  int64_t total, slot[kNumSlots], gpu;
  int64_t draws, verts, scopes;
};

struct Open {
  int node;
  int64_t start;
  FrameSlot slot;
  int gpu_begin;  // GpuMark index, or -1
};

// A section's GPU marks, waiting for EndFrame's resolve. sec indexes the
// spike frame's sections (mode 3), or is -1.
struct GpuSpan {
  int node, begin, end, sec;
};
std::vector<GpuSpan> g_gpu_spans;

std::vector<Node> g_nodes;
std::unordered_map<uint64_t, int> g_roots;
std::vector<Open> g_stack;
std::vector<Frame> g_frames;
// About 30 MB of frames; at 1000 fps, over eight minutes.
constexpr size_t kMaxFrames = 500000;
Frame g_cur;
int64_t g_frame_start = 0;
bool g_in_frame = false;
int64_t g_session_start = 0;
std::unordered_set<std::string> g_interned;

std::string TypeName(const Actor* a) {
  std::string s = typeid(*a).name();
  for (const char* p : {"class ", "struct "}) {
    if (s.rfind(p, 0) == 0) {
      s.erase(0, strlen(p));
    }
  }
  return s;
}

std::string ActorLabel(const Actor* a) {
  std::string s = TypeName(a);
  if (!a->GetName().empty()) {
    s += " \"" + a->GetName() + "\"";
  }
  return s;
}

std::string ActorPath(const Actor* a) {
  std::string s;
  for (const Actor* p = a; p != nullptr;
       p = const_cast<Actor*>(p)->GetParent()) {
    std::string part =
        p->GetName().empty() ? "<" + TypeName(p) + ">" : p->GetName();
    s = s.empty() ? part : part + "/" + s;
  }
  return s;
}

// ---- Mode 3: spike recorder ----

constexpr size_t kMaxPerFrame = 32;  // slow actors, events
constexpr size_t kHistory = 120;     // frames, for medians
constexpr size_t kMaxSpikes = 2000;
constexpr int64_t kWarmupNs = 2000000000;

// Where a frame's time went. blocked: wall time outside present the thread
// spent not running (I/O, locks, preemption); events: main-thread events.
enum Part {
  kPartUpdate,
  kPartDrawCpu,
  kPartGpu,
  kPartSwap,
  kPartFinish,
  kPartLimiter,
  kPartBlocked,
  kPartEvents,
  kNumParts
};
const char* const kPartNames[kNumParts] = {
    "update", "draw_cpu", "gpu",     "swap",
    "finish", "limiter",  "blocked", "events"};

struct Summary {
  int64_t total;
  int64_t part[kNumParts];
};

struct SectionTime {
  int node, depth;
  int64_t ns, gpu_ns;
};
struct SlowActor {
  std::string label, path;
  Kind kind;
  int64_t excl, incl;
};
struct EventRec {
  const char* type;
  std::string name;
  int depth;
  int64_t ns, bytes;
};
struct SlowOpen {
  const void* actor;  // nullptr once destroyed
  Kind kind;
  int64_t start, child;  // Ticks
};

// Process counters, read once per frame.
struct OsCounters {
  int64_t faults = 0, major = 0, switches = 0, ws = 0;
};

// The current frame's detail; cleared by FrameBegin.
struct SpikeFrame {
  std::vector<SectionTime> sections;
  std::vector<SlowActor> slow;
  std::vector<EventRec> events;
  int slow_dropped = 0, events_dropped = 0;
  int64_t events_ns = 0;  // top-level events only
  int64_t cpu0 = 0;       // ThreadCpuRaw at FrameBegin
};
SpikeFrame g_sf;
std::vector<SlowOpen> g_slow_stack;
int64_t g_slow_ns = 0;
int g_event_depth = 0;
std::thread::id g_main_thread;
Summary g_hist[kHistory];
size_t g_nframes = 0;  // frames seen this session
int64_t g_refresh_ns = 0;
bool g_vsync = false;
OsCounters g_os_prev;
int g_lua_prev_kb = 0;
int64_t g_tick0 = 0, g_ns0 = 0;  // Ticks and NowNs at Start
int64_t g_slow_ticks = 0;          // g_slow_ns in Ticks, set per frame

std::vector<std::string> g_spike_lines;
int g_spikes_dropped = 0;
// The last spike's record, open until the next frame's summary is added.
std::string g_pending;
struct SpikeInfo {
  double t, total_ms, song_s;
  int cause;  // Part, or -1
  bool warmup, unfocused;
  std::string screen;
};
std::vector<SpikeInfo> g_spike_info;
// "label [path]" or "type: name" -> (count, ns), over every spike.
std::map<std::string, std::pair<int, int64_t>> g_slow_agg, g_event_agg;

// A cheap clock for the per-actor-call timing: the TSC on x86-64 (a few ns
// to read, against about 25 for steady_clock), else NowNs.
inline int64_t Ticks() {
#if defined(_M_X64) || defined(__x86_64__)
  return static_cast<int64_t>(__rdtsc());
#else
  return NowNs();
#endif
}

// Calibrated against steady_clock over the session; 1 where Ticks is NowNs.
double TicksPerNs() {
  const int64_t dn = NowNs() - g_ns0;
  return dn > 0 ? static_cast<double>(Ticks() - g_tick0) / dn : 1.0;
}

// Thread CPU time in raw units; RawPerNs converts.
int64_t ThreadCpuRaw() {
#if defined(_WIN32)
  ULONG64 cycles = 0;
  QueryThreadCycleTime(GetCurrentThread(), &cycles);
  return static_cast<int64_t>(cycles);
#else
  timespec ts;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
#endif
}

// ponytail: QueryThreadCycleTime counts TSC ticks on invariant-TSC CPUs (all
// current x86-64), so the TSC rate over the session converts it. A CPU
// without one would show odd blocked times.
double RawPerNs() {
#if defined(_WIN32)
  return TicksPerNs();
#else
  return 1.0;
#endif
}

OsCounters ReadOs() {
  OsCounters c;
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    c.faults = pmc.PageFaultCount;
    c.ws = static_cast<int64_t>(pmc.WorkingSetSize);
  }
#else
  rusage ru{};
#if defined(__linux__)
  getrusage(RUSAGE_THREAD, &ru);
#else
  getrusage(RUSAGE_SELF, &ru);
#endif
  c.faults = ru.ru_minflt + ru.ru_majflt;
  c.major = ru.ru_majflt;
  c.switches = ru.ru_nivcsw;
#endif
  return c;
}

int LuaKB() {
  Lua* L = LUA->Get();
  const int kb = lua_gc(L, LUA_GCCOUNT, 0);
  LUA->Release(L);
  return kb;
}

std::string JsonStr(const std::string& in) {
  std::string out = "\"";
  for (unsigned char c : in) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      out += ssprintf("\\u%04x", c);
    } else {
      out += static_cast<char>(c);
    }
  }
  return out + "\"";
}

const char* KindName(Kind k) {
  return k == kUpdate ? "update" : k == kDraw ? "draw" : "command";
}

std::string SummaryJson(const Summary& f) {
  std::string out = ssprintf("{\"total_ms\": %.3f", f.total / 1e6);
  for (int i = 0; i < kNumParts; ++i) {
    out += ssprintf(", \"%s\": %.3f", kPartNames[i], f.part[i] / 1e6);
  }
  return out + "}";
}

// Median of the last n frames' values of one field (n <= kHistory).
int64_t HistMedian(size_t n, int part) {
  std::vector<int64_t> v(n);
  for (size_t i = 0; i < n; ++i) {
    const Summary& f = g_hist[(g_nframes - 1 - i) % kHistory];
    v[i] = part < 0 ? f.total : f.part[part];
  }
  std::nth_element(v.begin(), v.begin() + n / 2, v.end());
  return v[n / 2];
}

void RecordSpike(
    const Summary& f, int64_t threshold, int64_t cpu_ns, const OsCounters& os,
    int lua_kb) {
  const size_t nhist = std::min(g_nframes, kHistory);
  int64_t median[kNumParts] = {};
  int cause = -1;
  int64_t worst = 0;
  for (int i = 0; i < kNumParts; ++i) {
    median[i] = nhist ? HistMedian(nhist, i) : 0;
    if (f.part[i] - median[i] > worst) {
      worst = f.part[i] - median[i];
      cause = i;
    }
  }
  SpikeInfo info;
  info.t = (NowNs() - g_session_start) / 1e9;
  info.total_ms = f.total / 1e6;
  info.song_s = GAMESTATE->m_Position.m_fMusicSeconds;
  info.cause = cause;
  info.warmup = NowNs() - g_session_start < kWarmupNs;
  info.unfocused = !HOOKS->AppHasFocus();
  Screen* top = SCREENMAN ? SCREENMAN->GetTopScreen() : nullptr;
  info.screen = top ? top->GetName() : "";
  g_spike_info.push_back(info);

  std::string out = ssprintf(
      "{\"t\": %.3f, \"frame\": %d, \"screen\": %s, \"song_s\": %.3f, "
      "\"total_ms\": %.3f, \"threshold_ms\": %.3f, \"cause\": \"%s\", "
      "\"warmup\": %s, \"unfocused\": %s",
      info.t, static_cast<int>(g_nframes), JsonStr(info.screen).c_str(),
      info.song_s, info.total_ms, threshold / 1e6,
      cause >= 0 ? kPartNames[cause] : "unknown",
      info.warmup ? "true" : "false", info.unfocused ? "true" : "false");
  out += ", \"parts_ms\": " + SummaryJson(f);
  Summary med{nhist ? HistMedian(nhist, -1) : 0, {}};
  std::copy(std::begin(median), std::end(median), med.part);
  out += ", \"median_ms\": " + SummaryJson(med);
  out += ssprintf(
      ", \"thread_cpu_ms\": %.3f, \"page_faults\": %d, \"major_faults\": %d, "
      "\"invol_switches\": %d, \"working_set_delta_kb\": %d, \"lua_kb\": %d, "
      "\"lua_gc\": %s",
      cpu_ns / 1e6, static_cast<int>(os.faults - g_os_prev.faults),
      static_cast<int>(os.major - g_os_prev.major),
      static_cast<int>(os.switches - g_os_prev.switches),
      static_cast<int>((os.ws - g_os_prev.ws) / 1024), lua_kb,
      lua_kb < g_lua_prev_kb ? "true" : "false");

  // The slowest sections, with this session's average for comparison.
  std::vector<const SectionTime*> secs;
  for (const SectionTime& st : g_sf.sections) {
    secs.push_back(&st);
  }
  std::sort(secs.begin(), secs.end(), [](auto* a, auto* b) {
    return a->ns > b->ns;
  });
  out += ", \"sections\": [";
  for (size_t i = 0; i < std::min<size_t>(16, secs.size()); ++i) {
    const Node& n = g_nodes[secs[i]->node];
    out += ssprintf(
        "%s{\"name\": %s, \"depth\": %d, \"ms\": %.3f, \"gpu_ms\": %.3f, "
        "\"avg_ms\": %.3f}",
        i ? ", " : "", JsonStr(n.label).c_str(), secs[i]->depth,
        secs[i]->ns / 1e6, secs[i]->gpu_ns / 1e6,
        n.calls ? n.incl_ns / 1e6 / n.calls : 0.0);
  }
  out += "], \"slow_actors\": [";
  for (size_t i = 0; i < g_sf.slow.size(); ++i) {
    const SlowActor& a = g_sf.slow[i];
    out += ssprintf(
        "%s{\"actor\": %s, \"path\": %s, \"kind\": \"%s\", \"excl_ms\": %.3f, "
        "\"incl_ms\": %.3f}",
        i ? ", " : "", JsonStr(a.label).c_str(), JsonStr(a.path).c_str(),
        KindName(a.kind), a.excl / 1e6, a.incl / 1e6);
    auto& agg = g_slow_agg[a.label + "  [" + a.path + "]"];
    ++agg.first;
    agg.second += a.excl;
  }
  out += "], \"events\": [";
  for (size_t i = 0; i < g_sf.events.size(); ++i) {
    const EventRec& e = g_sf.events[i];
    out += ssprintf(
        "%s{\"type\": \"%s\", \"name\": %s, \"depth\": %d, \"ms\": %.3f, "
        "\"bytes\": %lld}",
        i ? ", " : "", e.type, JsonStr(e.name).c_str(), e.depth, e.ns / 1e6,
        static_cast<long long>(e.bytes));
    auto& agg = g_event_agg[std::string(e.type) + ": " + e.name];
    ++agg.first;
    agg.second += e.ns;
  }
  out += ssprintf(
      "], \"slow_actors_dropped\": %d, \"events_dropped\": %d, \"prev\": [",
      g_sf.slow_dropped, g_sf.events_dropped);
  for (size_t i = std::min<size_t>(3, g_nframes); i > 0; --i) {
    out += SummaryJson(g_hist[(g_nframes - i) % kHistory]);
    out += i > 1 ? ", " : "";
  }
  out += "]";
  g_pending = std::move(out);  // FrameEnd adds "next"
}

void FlushPending(const Summary* next) {
  if (g_pending.empty()) {
    return;
  }
  if (next) {
    g_pending += ", \"next\": " + SummaryJson(*next);
  }
  g_pending += "}";
  if (g_spike_lines.size() < kMaxSpikes) {
    g_spike_lines.push_back(std::move(g_pending));
  } else {
    ++g_spikes_dropped;
  }
  g_pending.clear();
}

void SpikeFrameEnd() {
  Summary f{g_cur.total, {}};
  f.part[kPartUpdate] = g_cur.slot[kSlotUpdate];
  f.part[kPartDrawCpu] = g_cur.slot[kSlotDraw] - g_cur.slot[kSlotPresent];
  f.part[kPartGpu] = g_cur.gpu;
  f.part[kPartSwap] = g_cur.slot[kSlotSwap];
  f.part[kPartFinish] = g_cur.slot[kSlotFinish];
  f.part[kPartLimiter] = g_cur.slot[kSlotLimiter];
  const int64_t cpu_ns =
      static_cast<int64_t>((ThreadCpuRaw() - g_sf.cpu0) / RawPerNs());
  // Swap, glFinish and the limiter sleep by design; count the rest.
  f.part[kPartBlocked] = std::max<int64_t>(
      0, f.total - f.part[kPartSwap] - f.part[kPartFinish] -
             f.part[kPartLimiter] - cpu_ns);
  f.part[kPartEvents] = g_sf.events_ns;
  const OsCounters os = ReadOs();
  const int lua_kb = LuaKB();

  FlushPending(&f);
  int64_t threshold = static_cast<int64_t>(g_fSpikeMs.Get() * 1e6);
  if (threshold <= 0 && g_vsync && g_refresh_ns > 0) {
    threshold = g_refresh_ns * 3 / 2;
  } else if (threshold <= 0 && g_nframes >= 30) {
    const int64_t m = HistMedian(std::min(g_nframes, kHistory), -1);
    threshold = std::max(2 * m, m + 4000000);
  }
  if (threshold > 0 && f.total > threshold) {
    RecordSpike(f, threshold, cpu_ns, os, lua_kb);
  }
  g_hist[g_nframes % kHistory] = f;
  ++g_nframes;
  g_os_prev = os;
  g_lua_prev_kb = lua_kb;
}

int NewNode(int parent, const void* key, Kind kind) {
  Node n;
  n.parent = parent;
  n.key = key;
  n.kind = kind;
  if (kind == kNamed) {
    n.label = static_cast<const char*>(key);
  } else {
    const Actor* a = static_cast<const Actor*>(key);
    n.label = ActorLabel(a);
    n.path = ActorPath(a);
  }
  g_nodes.push_back(std::move(n));
  return static_cast<int>(g_nodes.size()) - 1;
}

// v is sorted.
double Pct(const std::vector<int64_t>& v, double p) {
  if (v.empty()) {
    return 0;
  }
  size_t i =
      std::min(v.size() - 1, static_cast<size_t>(p * (v.size() - 1) + 0.5));
  return v[i] / 1e6;
}

void DumpLuaTable(
    lua_State* L, int depth, const std::string& indent, std::string& out) {
  // Sort keys so reports diff cleanly.
  std::vector<std::pair<std::string, std::string>> rows;
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    lua_pushvalue(L, -2);
    std::string k = lua_tostring(L, -1) ? lua_tostring(L, -1) : "?";
    lua_pop(L, 1);
    std::string v;
    if (lua_istable(L, -1)) {
      if (depth > 0) {
        DumpLuaTable(L, depth - 1, indent + "  ", v);
        v = "\n" + v;
      } else {
        v = "{...}";
      }
    } else if (lua_isboolean(L, -1)) {
      v = lua_toboolean(L, -1) ? "true" : "false";
    } else if (lua_isnumber(L, -1) || lua_isstring(L, -1)) {
      v = lua_tostring(L, -1);
    } else {
      v = lua_typename(L, lua_type(L, -1));
    }
    lua_pop(L, 1);
    rows.emplace_back(k, v);
  }
  std::sort(rows.begin(), rows.end());
  for (auto& r : rows) {
    out += indent + r.first + " = " + r.second;
    if (r.second.empty() || r.second[0] != '\n') {
      out += "\n";
    }
  }
}

std::string ThemeModifiers(PlayerNumber pn) {
  std::string out;
  Lua* L = LUA->Get();
  lua_getglobal(L, "SL");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, pn == PLAYER_1 ? "P1" : "P2");
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "ActiveModifiers");
      if (lua_istable(L, -1)) {
        DumpLuaTable(L, 2, "    ", out);
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  LUA->Release(L);
  return out;
}

void WriteTree(
    std::string& out, int idx, int depth, double nframes, double min_ms) {
  const Node& n = g_nodes[idx];
  double incl = n.incl_ns / 1e6 / nframes;
  double excl = (n.incl_ns - n.child_ns) / 1e6 / nframes;
  const std::string gpu =
      n.gpu_ns > 0 ? ssprintf("%8.3f", n.gpu_ns / 1e6 / nframes) : "";
  out += ssprintf(
      "%8.3f %8.3f %8s %8.1f %6.1f %8.0f  %s%s%s\n", incl, excl, gpu.c_str(),
      n.calls / nframes, n.draws / nframes, n.verts / nframes,
      std::string(depth * 2, ' ').c_str(), n.kind == kCommand ? "[cmd] " : "",
      n.label.c_str());
  std::vector<int> kids;
  for (auto& c : n.children) {
    kids.push_back(c.second);
  }
  std::sort(kids.begin(), kids.end(), [](int a, int b) {
    return g_nodes[a].incl_ns > g_nodes[b].incl_ns;
  });
  int hidden = 0;
  double hidden_ms = 0;
  for (int k : kids) {
    double ms = g_nodes[k].incl_ns / 1e6 / nframes;
    if (ms >= min_ms) {
      WriteTree(out, k, depth + 1, nframes, min_ms);
    } else {
      ++hidden;
      hidden_ms += ms;
    }
  }
  if (hidden) {
    out += ssprintf(
        "%8.3f %8s %8s %8s %6s %8s  %s(%d smaller children)\n", hidden_ms, "",
        "", "", "", "", std::string((depth + 1) * 2, ' ').c_str(), hidden);
  }
}

void WriteSpikeSummary(std::string& out, std::string& json, double secs) {
  int counted = 0, warmup = 0, unfocused = 0;
  int by_cause[kNumParts + 1] = {};  // last: unknown
  for (const SpikeInfo& s : g_spike_info) {
    if (s.unfocused) {
      ++unfocused;
    } else if (s.warmup) {
      ++warmup;
    } else {
      ++counted;
      ++by_cause[s.cause >= 0 ? s.cause : kNumParts];
    }
  }
  out += ssprintf(
      "\n=== Spikes (mode 3) ===\n"
      "%d spikes (%.1f/min) + %d in the first 2 s (warmup) + %d unfocused; "
      "%d records dropped.\nRecords: FrameSpikes-*.jsonl next to this file.\n"
      "Causes (largest excess over the 120-frame median):\n",
      counted, secs > 0 ? counted * 60 / secs : 0.0, warmup, unfocused,
      g_spikes_dropped);
  json += ssprintf(
      ",\n  \"spikes\": {\"count\": %d, \"warmup\": %d, \"unfocused\": %d, "
      "\"dropped\": %d, \"causes\": {",
      counted, warmup, unfocused, g_spikes_dropped);
  bool first = true;
  for (int i = 0; i <= kNumParts; ++i) {
    if (by_cause[i] == 0) {
      continue;
    }
    const char* name = i < kNumParts ? kPartNames[i] : "unknown";
    out += ssprintf("  %-10s %6d\n", name, by_cause[i]);
    json += ssprintf("%s\"%s\": %d", first ? "" : ", ", name, by_cause[i]);
    first = false;
  }
  json += "}}";

  std::vector<size_t> worst(g_spike_info.size());
  for (size_t i = 0; i < worst.size(); ++i) {
    worst[i] = i;
  }
  std::sort(worst.begin(), worst.end(), [](size_t a, size_t b) {
    return g_spike_info[a].total_ms > g_spike_info[b].total_ms;
  });
  out += "Worst spikes:      t(s)  song(s)  total(ms)  cause     screen\n";
  for (size_t i = 0; i < std::min<size_t>(20, worst.size()); ++i) {
    const SpikeInfo& s = g_spike_info[worst[i]];
    out += ssprintf(
        "  %16.3f %8.2f %10.3f  %-9s %s%s%s\n", s.t, s.song_s, s.total_ms,
        s.cause >= 0 ? kPartNames[s.cause] : "unknown", s.screen.c_str(),
        s.warmup ? " (warmup)" : "", s.unfocused ? " (unfocused)" : "");
  }
  auto top = [&out](
                 const char* title,
                 const std::map<std::string, std::pair<int, int64_t>>& agg) {
    std::vector<const std::pair<const std::string, std::pair<int, int64_t>>*>
        rows;
    for (const auto& r : agg) {
      rows.push_back(&r);
    }
    std::sort(rows.begin(), rows.end(), [](auto* a, auto* b) {
      return a->second.second > b->second.second;
    });
    out += ssprintf("%s (spikes, total ms):\n", title);
    for (size_t i = 0; i < std::min<size_t>(20, rows.size()); ++i) {
      out += ssprintf(
          "  %5d %9.3f  %s\n", rows[i]->second.first,
          rows[i]->second.second / 1e6, rows[i]->first.c_str());
    }
  };
  top("Slow actors in spike frames", g_slow_agg);
  top("Events in spike frames", g_event_agg);
}

void WriteReport() {
  if (g_frames.empty()) {
    return;
  }
  const double nframes = static_cast<double>(g_frames.size());
  const double secs = (NowNs() - g_session_start) / 1e9;
  std::string out;

  out += "=== ITGmania render profile (ScreenGameplay) ===\n";
  out += g_actor_scopes
             ? "Profiler mode: 1 (full; per-actor timings inflated)\n"
             : "Profiler mode: 2 (light)\n";
  if (const Song* song = GAMESTATE->m_pCurSong) {
    out += "Song: " + song->GetDisplayFullTitle() + "\n";
  }
  out += ssprintf(
      "Music rate: %.2f\n", GAMESTATE->m_SongOptions.GetCurrent().m_fMusicRate);
  ActualVideoModeParams vm = DISPLAY->GetActualVideoModeParams();
  out += ssprintf(
      "Renderer: %s  %dx%d @%dHz  vsync=%d windowed=%d\n",
      DISPLAY->GetApiDescription().c_str(), vm.width, vm.height, vm.rate,
      vm.vsync, vm.windowed);
  FOREACH_EnabledPlayer(pn) {
    out += ssprintf("\n-- Player %d --\n", pn + 1);
    if (const Steps* st = GAMESTATE->m_pCurSteps[pn]) {
      out += ssprintf(
          "Steps: %s %d\n", DifficultyToString(st->GetDifficulty()).c_str(),
          st->GetMeter());
    }
    out +=
        "Engine mods: " +
        GAMESTATE->m_pPlayerState[pn]->m_PlayerOptions.GetCurrent().GetString(
            true) +
        "\n";
    // Simply Love's; other themes have no such table.
    const std::string sThemeMods = ThemeModifiers(pn);
    if (!sThemeMods.empty()) {
      out += "Theme mods (SL.ActiveModifiers):\n" + sThemeMods;
    }
  }

  // Per-frame stats.
  std::vector<int64_t> total, slots[kNumSlots], work, gpu;
  int64_t draws = 0, verts = 0, scopes = 0;
  for (const Frame& f : g_frames) {
    total.push_back(f.total);
    for (int s = 0; s < kNumSlots; ++s) {
      slots[s].push_back(f.slot[s]);
    }
    work.push_back(f.total - f.slot[kSlotPresent]);
    gpu.push_back(f.gpu);
    draws += f.draws;
    verts += f.verts;
    scopes += f.scopes;
  }
  if (g_frames.size() >= kMaxFrames) {
    out += ssprintf(
        "\n(Frame stats cover the first %d frames only.)\n",
        static_cast<int>(kMaxFrames));
  }
  out += ssprintf(
      "\n=== Frames: %d over %.1fs (%.1f fps avg) ===\n"
      "Draw calls/frame: %.0f   verts/frame: %.0f   profiler scopes/frame: "
      "%.0f\n"
      "(times in ms)          mean      p50      p95      p99      max\n",
      static_cast<int>(nframes), secs, nframes / secs, draws / nframes,
      verts / nframes, scopes / nframes);
  // Machine-readable copy of the summary, for comparing runs with a script.
  std::string json = ssprintf(
      "{\n  \"frames\": %d,\n  \"seconds\": %.3f,\n"
      "  \"draws_per_frame\": %.2f,\n  \"verts_per_frame\": %.1f,\n",
      static_cast<int>(nframes), secs, draws / nframes, verts / nframes);
  auto row = [&](const char* name, const char* key, std::vector<int64_t>& v) {
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (int64_t x : v) {
      sum += x;
    }
    double mean = sum / v.size() / 1e6;
    out += ssprintf(
        "%-20s %8.3f %8.3f %8.3f %8.3f %8.3f\n", name, mean, Pct(v, .5),
        Pct(v, .95), Pct(v, .99), Pct(v, 1));
    json += ssprintf(
        "  \"%s_ms\": {\"mean\": %.4f, \"p50\": %.4f, \"p95\": %.4f, "
        "\"p99\": %.4f, \"max\": %.4f},\n",
        key, mean, Pct(v, .5), Pct(v, .95), Pct(v, .99), Pct(v, 1));
  };
  row("frame total", "frame", total);
  row("update", "update", slots[kSlotUpdate]);
  row("draw (incl present)", "draw", slots[kSlotDraw]);
  row("present/EndFrame", "present", slots[kSlotPresent]);
  row("  frame limiter", "limiter", slots[kSlotLimiter]);
  row("  SwapBuffers", "swap", slots[kSlotSwap]);
  row("  glFinish", "finish", slots[kSlotFinish]);
  row("CPU work (no pres.)", "cpu_work", work);
  if (g_gpu) {
    row("GPU (frame)", "gpu", gpu);
  }
  out +=
      "Note: SwapBuffers is mostly the vsync wait; glFinish is the GPU "
      "finishing the frame. GPU is BeginFrame to the last draw, from timer "
      "queries.\n";

  static const char* kGLNames[kNumGLStats] = {
      "matrix load", "vertex setup", "texture",  "blend",        "depth",
      "cull",        "alpha test",   "lighting", "glGet (sync)", "other"};
  int64_t gl_total = 0;
  for (int64_t n : g_gl) {
    gl_total += n;
  }
  out += ssprintf("\nGL state calls/frame: %.1f total\n", gl_total / nframes);
  for (int i = 0; i < kNumGLStats; ++i) {
    out += ssprintf("  %-14s %8.1f\n", kGLNames[i], g_gl[i] / nframes);
  }
  json += ssprintf(
      "  \"gl_calls_per_frame\": {\"total\": %.2f", gl_total / nframes);
  for (int i = 0; i < kNumGLStats; ++i) {
    json += ssprintf(", \"%s\": %.2f", kGLNames[i], g_gl[i] / nframes);
  }
  json += "}";

  std::vector<size_t> worst(g_frames.size());
  for (size_t i = 0; i < worst.size(); ++i) {
    worst[i] = i;
  }
  std::sort(worst.begin(), worst.end(), [](size_t a, size_t b) {
    return g_frames[a].total > g_frames[b].total;
  });
  out += "\nWorst frames (ms): #frame total update draw present draws\n";
  for (size_t i = 0; i < std::min<size_t>(10, worst.size()); ++i) {
    const Frame& f = g_frames[worst[i]];
    out += ssprintf(
        "  #%-6d %8.3f %8.3f %8.3f %8.3f %6d\n", static_cast<int>(worst[i]),
        f.total / 1e6, f.slot[kSlotUpdate] / 1e6, f.slot[kSlotDraw] / 1e6,
        f.slot[kSlotPresent] / 1e6, static_cast<int>(f.draws));
  }

  // Call tree.
  out +=
      "\n=== Call tree (per-frame averages; incl/excl/gpu in ms) ===\n"
      "    incl     excl      gpu    calls  draws    verts  node\n";
  std::vector<int> roots;
  for (size_t i = 0; i < g_nodes.size(); ++i) {
    if (g_nodes[i].parent < 0) {
      roots.push_back(static_cast<int>(i));
    }
  }
  std::sort(roots.begin(), roots.end(), [](int a, int b) {
    return g_nodes[a].incl_ns > g_nodes[b].incl_ns;
  });
  for (int r : roots) {
    WriteTree(out, r, 0, nframes, 0.005);
  }

  // Flat list by actor, exclusive time summed over every call site.
  struct Flat {
    std::string label, path;
    int64_t excl[3] = {0, 0, 0};
    int64_t draws = 0, calls = 0;
  };
  std::unordered_map<const void*, Flat> flat;
  for (const Node& n : g_nodes) {
    if (n.kind == kNamed) {
      continue;
    }
    Flat& f = flat[n.key];
    f.label = n.label;
    f.path = n.path;
    f.excl[n.kind] += n.incl_ns - n.child_ns;
    f.draws += n.draws;
    f.calls += n.calls;
  }
  // Command name nodes are children of the actor's command node; fold them in.
  for (const Node& n : g_nodes) {
    if (n.kind == kNamed && n.parent >= 0 &&
        g_nodes[n.parent].kind == kCommand) {
      flat[g_nodes[n.parent].key].excl[kCommand] += n.incl_ns - n.child_ns;
    }
  }
  std::vector<const Flat*> sorted;
  for (auto& f : flat) {
    sorted.push_back(&f.second);
  }
  auto sum = [](const Flat* f) { return f->excl[0] + f->excl[1] + f->excl[2]; };
  std::sort(sorted.begin(), sorted.end(), [&](const Flat* a, const Flat* b) {
    return sum(a) > sum(b);
  });
  out +=
      "\n=== Top actors by exclusive time (ms/frame) ===\n"
      "   total   update     draw      cmd  draws  actor  [static path]\n";
  for (size_t i = 0; i < std::min<size_t>(80, sorted.size()); ++i) {
    const Flat* f = sorted[i];
    out += ssprintf(
        "%8.3f %8.3f %8.3f %8.3f %6.1f  %s  [%s]\n", sum(f) / 1e6 / nframes,
        f->excl[kUpdate] / 1e6 / nframes, f->excl[kDraw] / 1e6 / nframes,
        f->excl[kCommand] / 1e6 / nframes, f->draws / nframes, f->label.c_str(),
        f->path.c_str());
  }

  time_t now = time(nullptr);
  char stamp[32];
  strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&now));
  std::string base = std::string("/Logs/RenderProfile-") + stamp;
  std::string jsonl;
  if (g_spikes) {
    WriteSpikeSummary(out, json, secs);
    for (const std::string& line : g_spike_lines) {
      jsonl += line + "\n";
    }
  }
  json += "\n}\n";
  std::vector<std::pair<std::string, std::string*>> files = {
      {base + ".txt", &out}, {base + ".json", &json}};
  if (!jsonl.empty()) {
    files.emplace_back(
        std::string("/Logs/FrameSpikes-") + stamp + ".jsonl", &jsonl);
  }
  for (const auto& f : files) {
    RageFile file;
    if (file.Open(f.first, RageFile::WRITE)) {
      file.Write(*f.second);
      file.Close();
      LOG->Info(
          "RenderProfiler: wrote %s (%d frames)", f.first.c_str(),
          static_cast<int>(nframes));
    } else {
      LOG->Warn("RenderProfiler: couldn't write %s", f.first.c_str());
    }
  }
}

}  // namespace

void Start() {
  if (g_iMode.Get() <= 0) {
    return;
  }
  ++g_session;
  const int mode = g_iMode.Get();
  g_actor_scopes = mode == 1;
  // Mode 1's inflated CPU times make GPU numbers meaningless next to them.
  g_gpu = mode >= 2;
  g_spikes = g_slow_actors = mode == 3;
  if (g_spikes) {
    g_sf = SpikeFrame();
    g_slow_stack.clear();
    g_slow_ns = static_cast<int64_t>(g_fSlowActorMs.Get() * 1e6);
    g_event_depth = 0;
    g_main_thread = std::this_thread::get_id();
    g_nframes = 0;
    g_spike_lines.clear();
    g_spikes_dropped = 0;
    g_pending.clear();
    g_spike_info.clear();
    g_slow_agg.clear();
    g_event_agg.clear();
    // Cached: copying the params every frame allocates.
    const ActualVideoModeParams vm = DISPLAY->GetActualVideoModeParams();
    g_vsync = vm.vsync;
    g_refresh_ns = vm.rate > 0 ? 1000000000 / vm.rate : 0;
    g_os_prev = ReadOs();
    g_lua_prev_kb = LuaKB();
    g_tick0 = Ticks();
    g_slow_ticks = INT64_MAX;  // set per frame once the TSC rate is known
    g_ns0 = NowNs();
  }
  std::fill(std::begin(g_gl), std::end(g_gl), 0);
  g_nodes.clear();
  g_roots.clear();
  g_stack.clear();
  g_frames.clear();
  g_in_frame = false;
  g_session_start = NowNs();
  g_active = true;
}

void Stop() {
  if (!g_active) {
    return;
  }
  g_active = false;
  g_stack.clear();
  FlushPending(nullptr);
  WriteReport();
  g_gpu = false;
  g_spikes = g_slow_actors = false;
  g_slow_stack.clear();
  g_spike_lines.clear();
  g_spike_info.clear();
  g_nodes.clear();
  g_roots.clear();
  g_frames.clear();
}

void FrameBegin() {
  if (!g_active) {
    return;
  }
  g_cur = Frame{};
  g_gpu_spans.clear();
  if (g_spikes) {
    g_sf.sections.clear();
    g_sf.slow.clear();
    g_sf.events.clear();
    g_sf.slow_dropped = g_sf.events_dropped = 0;
    g_sf.events_ns = 0;
    g_sf.cpu0 = ThreadCpuRaw();
    g_slow_ticks = static_cast<int64_t>(g_slow_ns * TicksPerNs());
  }
  g_frame_start = NowNs();
  g_in_frame = true;
}

void FrameEnd() {
  if (!g_active || !g_in_frame) {
    return;
  }
  g_cur.total = NowNs() - g_frame_start;
  if (g_frames.size() < kMaxFrames) {
    g_frames.push_back(g_cur);
  }
  if (g_spikes) {
    SpikeFrameEnd();
  }
  g_in_frame = false;
}

bool Push(const void* key, Kind kind, FrameSlot slot) {
  int parent = g_stack.empty() ? -1 : g_stack.back().node;
  // RunCommands reached via PlayCommandNoRecurse is already attributed.
  if (kind == kCommand && parent >= 0) {
    const Node& p = g_nodes[parent];
    if ((p.kind == kCommand && p.key == key) ||
        (p.kind == kNamed && p.parent >= 0 &&
         g_nodes[p.parent].kind == kCommand && g_nodes[p.parent].key == key)) {
      return false;
    }
  }
  uint64_t ck = (reinterpret_cast<uintptr_t>(key) << 2) | kind;
  int idx;
  auto& siblings = parent < 0 ? g_roots : g_nodes[parent].children;
  auto it = siblings.find(ck);
  if (it != siblings.end()) {
    idx = it->second;
  } else {
    idx = NewNode(parent, key, kind);
    // NewNode may reallocate g_nodes; look the map up again.
    (parent < 0 ? g_roots : g_nodes[parent].children).emplace(ck, idx);
  }
  ++g_cur.scopes;
  const int gpu_begin = g_gpu && kind == kNamed ? DISPLAY->GpuMark() : -1;
  g_stack.push_back({idx, NowNs(), slot, gpu_begin});
  return true;
}

void Pop() {
  if (!g_active || g_stack.empty()) {
    return;
  }
  Open o = g_stack.back();
  g_stack.pop_back();
  int64_t dt = NowNs() - o.start;
  Node& n = g_nodes[o.node];
  n.incl_ns += dt;
  ++n.calls;
  if (n.parent >= 0) {
    g_nodes[n.parent].child_ns += dt;
  }
  if (o.slot != kSlotNone) {
    g_cur.slot[o.slot] += dt;
  }
  int sec = -1;
  if (g_spikes && n.kind == kNamed) {
    sec = static_cast<int>(g_sf.sections.size());
    g_sf.sections.push_back(
        {o.node, static_cast<int>(g_stack.size()), dt, 0});
  }
  if (o.gpu_begin >= 0) {
    const int end = DISPLAY->GpuMark();
    if (end >= 0) {
      g_gpu_spans.push_back({o.node, o.gpu_begin, end, sec});
    }
  }
}

bool SlowBegin(const void* actor, Kind kind) {
  g_slow_stack.push_back({actor, kind, Ticks(), 0});
  return true;
}

void SlowEnd() {
  if (g_slow_stack.empty()) {
    return;
  }
  const SlowOpen o = g_slow_stack.back();
  g_slow_stack.pop_back();
  const int64_t dt = Ticks() - o.start;
  if (!g_slow_stack.empty()) {
    g_slow_stack.back().child += dt;
  }
  const int64_t excl = dt - o.child;
  if (excl < g_slow_ticks) {
    return;
  }
  if (g_sf.slow.size() >= kMaxPerFrame) {
    ++g_sf.slow_dropped;
    return;
  }
  const Actor* a = static_cast<const Actor*>(o.actor);
  const double per_ns = TicksPerNs();
  g_sf.slow.push_back(
      {a ? ActorLabel(a) : "<destroyed during the call>",
       a ? ActorPath(a) : "", o.kind, static_cast<int64_t>(excl / per_ns),
       static_cast<int64_t>(dt / per_ns)});
}

void ActorGoneSlow(const Actor* a) {
  for (SlowOpen& o : g_slow_stack) {
    if (o.actor == a) {
      o.actor = nullptr;
    }
  }
}

int64_t EventBegin() {
  if (std::this_thread::get_id() != g_main_thread) {
    return -1;
  }
  ++g_event_depth;
  return NowNs();
}

void EventEnd(
    const char* type, const std::string& name, int64_t start, int64_t bytes) {
  --g_event_depth;
  if (!g_spikes) {
    return;
  }
  const int64_t dt = NowNs() - start;
  if (g_event_depth == 0) {
    g_sf.events_ns += dt;
  }
  if (g_sf.events.size() >= kMaxPerFrame) {
    ++g_sf.events_dropped;
    return;
  }
  g_sf.events.push_back({type, name, g_event_depth, dt, bytes});
}

void GpuResolved(const uint64_t* times, int n) {
  if (!g_active || n < 2) {
    return;
  }
  for (const GpuSpan& span : g_gpu_spans) {
    const int64_t ns =
        static_cast<int64_t>(times[span.end] - times[span.begin]);
    g_nodes[span.node].gpu_ns += ns;
    if (span.sec >= 0 && span.sec < static_cast<int>(g_sf.sections.size())) {
      g_sf.sections[span.sec].gpu_ns = ns;
    }
  }
  g_gpu_spans.clear();
  g_cur.gpu = static_cast<int64_t>(times[n - 1] - times[0]);
}

void AddDraw(int verts) {
  if (!g_active) {
    return;
  }
  ++g_cur.draws;
  g_cur.verts += verts;
  if (!g_stack.empty()) {
    Node& n = g_nodes[g_stack.back().node];
    ++n.draws;
    n.verts += verts;
  }
}

bool PushGroup(const void* key, Kind kind, FrameSlot slot) {
  // The renderer's Init draws a frame before DISPLAY is set.
  if (DISPLAY == nullptr) {
    return false;
  }
  // Frame slots (Update, Draw, Present) span the present; leave them out.
  if (kind == kNamed && slot == kSlotNone) {
    return DISPLAY->PushDebugGroup(static_cast<const char*>(key));
  }
  if (kind == kDraw && g_debug_groups >= 2) {
    return DISPLAY->PushDebugGroup(
        ActorLabel(static_cast<const Actor*>(key)).c_str());
  }
  return false;
}

void PopGroup() { DISPLAY->PopDebugGroup(); }

const char* Intern(const std::string& s) {
  return g_interned.insert(s).first->c_str();
}

const char* InternName(const Message& msg) { return Intern(msg.GetName()); }

}  // namespace RenderProfiler
