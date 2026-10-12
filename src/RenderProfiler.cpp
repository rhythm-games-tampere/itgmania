#include "RenderProfiler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Actor.h"
#include "GameState.h"
#include "LuaManager.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "Preference.h"
#include "RageDisplay.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongOptions.h"
#include "Steps.h"

namespace RenderProfiler {

bool g_active = false;
bool g_actor_scopes = false;
int64_t g_gl[kNumGLStats];
uint32_t g_session = 0;

namespace {

Preference<int> g_iMode("RenderProfiler", 0);

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
  int64_t calls = 0;
  int64_t draws = 0;
  int64_t verts = 0;
  std::unordered_map<uint64_t, int> children;
};

struct Frame {
  int64_t total, slot[kNumSlots];
  int64_t draws, verts, scopes;
};

struct Open {
  int node;
  int64_t start;
  FrameSlot slot;
};

std::vector<Node> g_nodes;
std::unordered_map<uint64_t, int> g_roots;
std::vector<Open> g_stack;
std::vector<Frame> g_frames;
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

double Pct(std::vector<int64_t> v, double p) {
  if (v.empty()) {
    return 0;
  }
  std::sort(v.begin(), v.end());
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
  out += ssprintf(
      "%8.3f %8.3f %8.1f %6.1f %8.0f  %s%s%s\n", incl, excl, n.calls / nframes,
      n.draws / nframes, n.verts / nframes, std::string(depth * 2, ' ').c_str(),
      n.kind == kCommand ? "[cmd] " : "", n.label.c_str());
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
        "%8.3f %8s %8s %6s %8s  %s(%d smaller children)\n", hidden_ms, "", "",
        "", "", std::string((depth + 1) * 2, ' ').c_str(), hidden);
  }
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
    out += "Theme mods (SL.ActiveModifiers):\n" + ThemeModifiers(pn);
  }

  // Per-frame stats.
  std::vector<int64_t> total, slots[kNumSlots], work;
  int64_t draws = 0, verts = 0, scopes = 0;
  for (const Frame& f : g_frames) {
    total.push_back(f.total);
    for (int s = 0; s < kNumSlots; ++s) {
      slots[s].push_back(f.slot[s]);
    }
    work.push_back(f.total - f.slot[kSlotPresent]);
    draws += f.draws;
    verts += f.verts;
    scopes += f.scopes;
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
  auto row = [&](const char* name, const char* key,
                 const std::vector<int64_t>& v) {
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
  row("CPU work (no pres.)", "cpu_work", work);
  out +=
      "Note: present/EndFrame includes vsync wait and any GPU stall; "
      "GPU-bound work shows up there.\n";

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
  json += "}\n}\n";

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
      "\n=== Call tree (per-frame averages; incl/excl in ms) ===\n"
      "    incl     excl    calls  draws    verts  node\n";
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
  for (const auto& f :
       {std::make_pair(base + ".txt", &out),
        std::make_pair(base + ".json", &json)}) {
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
  g_actor_scopes = g_iMode.Get() == 1;
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
  WriteReport();
  g_nodes.clear();
  g_roots.clear();
  g_frames.clear();
}

void FrameBegin() {
  if (!g_active) {
    return;
  }
  g_cur = Frame{};
  g_frame_start = NowNs();
  g_in_frame = true;
}

void FrameEnd() {
  if (!g_active || !g_in_frame) {
    return;
  }
  g_cur.total = NowNs() - g_frame_start;
  g_frames.push_back(g_cur);
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
  g_stack.push_back({idx, NowNs(), slot});
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

const char* Intern(const std::string& s) {
  return g_interned.insert(s).first->c_str();
}

}  // namespace RenderProfiler
