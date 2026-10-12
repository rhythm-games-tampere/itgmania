#include "RageDisplay_OGL.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Actor.h"
#include "DisplaySpec.h"
#include "EnumHelper.h"
#include "LocalizedString.h"
#include "ModelTypes.h"
#include "Preference.h"
#include "RageDisplay.h"
#include "RageDisplay_OGL_Helpers.h"
#include "RageException.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageMath.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageTextureID.h"
#include "RageTextureManager.h"
#include "RageTextureRenderTarget.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/Endian.h"
#include "RenderProfiler.h"
#include "Sprite.h"
#include "arch/LowLevelWindow/LowLevelWindow.h"
#include "global.h"

using namespace RageDisplay_Legacy_Helpers;

#if defined(_MSC_VER)
#pragma comment(lib, "opengl32.lib")
#endif

#ifdef NO_GL_FLUSH
#define glFlush()
#endif

//
// Globals
//

static bool g_bReversePackedPixelsWorks = true;

/* OpenGL system information that generally doesn't change at runtime. */

/* OpenGL version * 10: */
static int g_glVersion;

static int g_iMaxTextureUnits = 0;

/* Vertex attribute locations, bound before linking. Position must be 0. */
static const GLuint kAttribPosition = 0;
static const GLuint kAttribColor = 1;
static const GLuint kAttribTexCoord = 2;
static const GLuint kAttribNormal = 3;
static const GLuint kAttribTextureMatrixScale = 6;  // RageModelVertex

static std::map<uintptr_t, RenderTarget*> g_mapRenderTargets;
static RenderTarget* g_pCurrentRenderTarget = nullptr;

static LowLevelWindow* g_pWind;

static bool g_bInvertY = false;

static void InvalidateObjects();

static RageDisplay::RagePixelFormatDesc PIXEL_FORMAT_DESC[NUM_RagePixelFormat] =
    {{/* R8G8B8A8 */
      32,
      {0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF}},
     {/* B8G8R8A8 */
      32,
      {0x0000FF00, 0x00FF0000, 0xFF000000, 0x000000FF}},
     {
         /* R4G4B4A4 */
         16,
         {0xF000, 0x0F00, 0x00F0, 0x000F},
     },
     {
         /* R5G5B5A1 */
         16,
         {0xF800, 0x07C0, 0x003E, 0x0001},
     },
     {
         /* R5G5B5X1 */
         16,
         {0xF800, 0x07C0, 0x003E, 0x0000},
     },
     {/* R8G8B8 */
      24,
      {0xFF0000, 0x00FF00, 0x0000FF, 0x000000}},
     {
         /* Paletted */
         8,
         {0, 0, 0, 0} /* N/A */
     },
     {/* B8G8R8 */
      24,
      {0x0000FF, 0x00FF00, 0xFF0000, 0x000000}},
     {
         /* A1R5G5B5 */
         16,
         {0x7C00, 0x03E0, 0x001F, 0x8000},
     },
     {
         /* X1R5G5B5 */
         16,
         {0x7C00, 0x03E0, 0x001F, 0x0000},
     }};

/* g_GLPixFmtInfo is used for both texture formats and surface formats.  For
 * example, it's fine to ask for a RagePixelFormat_RGB5 texture, but to supply a
 * surface matching RagePixelFormat_RGB8.  OpenGL will simply discard the extra
 * bits.
 *
 * It's possible for a format to be supported as a texture format but not as a
 * surface format.  For example, if packed pixels aren't supported, we can still
 * use GL_RGB5_A1, but we'll have to convert to a supported surface pixel format
 * first.  It's not ideal, since we'll convert to RGBA8 and OGL will convert
 * back, but it works fine.
 */
struct GLPixFmtInfo_t {
  GLenum internalfmt; /* target format */
  GLenum format;      /* target format */
  GLenum type;        /* data format */
} const g_GLPixFmtInfo[NUM_RagePixelFormat] = {
    {
        /* R8G8B8A8 */
        GL_RGBA8,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    },
    {
        /* R8G8B8A8 */
        GL_RGBA8,
        GL_BGRA,
        GL_UNSIGNED_BYTE,
    },
    {
        /* B4G4R4A4 */
        GL_RGBA4,
        GL_RGBA,
        GL_UNSIGNED_SHORT_4_4_4_4,
    },
    {
        /* B5G5R5A1 */
        GL_RGB5_A1,
        GL_RGBA,
        GL_UNSIGNED_SHORT_5_5_5_1,
    },
    {
        /* B5G5R5 */
        GL_RGB5,
        GL_RGBA,
        GL_UNSIGNED_SHORT_5_5_5_1,
    },
    {
        /* B8G8R8 */
        GL_RGB8,
        GL_RGB,
        GL_UNSIGNED_BYTE,
    },
    {
        /* Paletted: not supported; paletted surfaces are expanded to RGBA8
         * by GetImgPixelFormat. */
        0,
        0,
        0,
    },
    {
        /* B8G8R8 */
        GL_RGB8,
        GL_BGR,
        GL_UNSIGNED_BYTE,
    },
    {
        /* A1R5G5B5 (matches D3DFMT_A1R5G5B5) */
        GL_RGB5_A1,
        GL_BGRA,
        GL_UNSIGNED_SHORT_1_5_5_5_REV,
    },
    {
        /* X1R5G5B5 */
        GL_RGB5,
        GL_BGRA,
        GL_UNSIGNED_SHORT_1_5_5_5_REV,
    }};

static void FixLittleEndian() {
  if constexpr (!Endian::little) {
    return;
  }

  static bool bInitialized = false;
  if (bInitialized) {
    return;
  }
  bInitialized = true;

  for (int i = 0; i < NUM_RagePixelFormat; ++i) {
    RageDisplay::RagePixelFormatDesc& pf = PIXEL_FORMAT_DESC[i];

    /* OpenGL and RageSurface handle byte formats differently; we need
     * to flip non-paletted masks to make them line up. */
    if (g_GLPixFmtInfo[i].type != GL_UNSIGNED_BYTE || pf.bpp == 8) {
      continue;
    }

    for (int mask = 0; mask < 4; ++mask) {
      int m = pf.masks[mask];
      switch (pf.bpp) {
        case 24:
          m = Swap24(m);
          break;
        case 32:
          m = Swap32(m);
          break;
        default:
          FAIL_M(ssprintf("Unsupported BPP value: %i", pf.bpp));
      }
      pf.masks[mask] = m;
    }
  }
}

/* Shadow copy of the GL state we set per draw, so redundant calls are skipped.
 * Mesa revalidates state on the CPU after every state call, changed or not, and
 * glGet* stalls mesa_glthread. -1 / false means unknown: the next set always
 * reaches GL. thread_local because concurrent rendering draws from a second
 * context on another thread. Invalidated on frame start, render target and
 * resolution changes, and concurrent rendering hand-offs. */
namespace {
using RenderProfiler::CountGL;

struct GLStateCache {
  int active_unit;
  int64_t bound_tex[NUM_TextureUnit];  // -1 unknown
  int blend, zwrite, ztest, cull;
  float zbias;
  bool zbias_valid;
  int64_t program;                       // -1 unknown
  int attrib[8];                         // vertex attribute arrays enabled
  int64_t array_buffer, element_buffer;  // -1 unknown

  void Invalidate() {
    active_unit = -1;
    for (int i = 0; i < NUM_TextureUnit; ++i) {
      bound_tex[i] = -1;
    }
    blend = zwrite = ztest = cull = -1;
    zbias_valid = false;
    program = -1;
    std::fill(std::begin(attrib), std::end(attrib), -1);
    array_buffer = element_buffer = -1;
  }
};

// Per texture object: filter/wrap last set, and whether it has mip levels.
struct TexParams {
  int filter = -1;
  int wrap = -1;
  int mipmapped = -1;
};

thread_local GLStateCache g_cache = [] {
  GLStateCache c;
  c.Invalidate();
  return c;
}();
thread_local std::unordered_map<GLuint, TexParams> g_tex_params;

void InvalidateGLStateCache() {
  g_cache.Invalidate();
  g_tex_params.clear();
}

// Core profiles draw nothing without a vertex array object, and they aren't
// shared between contexts: each context gets one and keeps it bound. Needs a
// current context.
static void BindVertexArrayObject() {
  if (!GLAD_GL_VERSION_3_0) {
    return;  // macOS's 2.1 compatibility context; it has a default one
  }
  GLint iBound = 0;
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &iBound);
  if (iBound == 0) {
    GLuint id = 0;
    glGenVertexArrays(1, &id);
    glBindVertexArray(id);
  }
}

void CacheActiveUnit(int unit) {
  if (g_cache.active_unit == unit) {
    return;
  }
  CountGL(RenderProfiler::kGLTexture);
  glActiveTexture(enum_add2(GL_TEXTURE0, unit));
  g_cache.active_unit = unit;
}

void CacheAttribArray(GLuint index, bool on) {
  if (g_cache.attrib[index] == static_cast<int>(on)) {
    return;
  }
  CountGL(RenderProfiler::kGLVertexSetup);
  if (on) {
    glEnableVertexAttribArray(index);
  } else {
    glDisableVertexAttribArray(index);
  }
  g_cache.attrib[index] = on;
}

// Binds on the active unit, like glBindTexture.
void CacheBindTexture(GLuint id) {
  int unit = g_cache.active_unit;
  if (unit >= 0 && g_cache.bound_tex[unit] == id) {
    return;
  }
  CountGL(RenderProfiler::kGLTexture);
  glBindTexture(GL_TEXTURE_2D, id);
  if (unit >= 0) {
    g_cache.bound_tex[unit] = id;
  } else {
    for (int64_t& b : g_cache.bound_tex) {
      b = -1;
    }
  }
}

void CacheBindBuffer(GLenum target, int64_t& cached, GLuint id) {
  if (cached == id) {
    return;
  }
  CountGL(RenderProfiler::kGLVertexSetup);
  glBindBuffer(target, id);
  cached = id;
}

void CacheUseProgram(GLuint program) {
  if (g_cache.program == program) {
    return;
  }
  CountGL(RenderProfiler::kGLOther);
  glUseProgram(program);
  g_cache.program = program;
}

/* Bumped when TryVideoMode creates a new GL context; buffers made in the old
 * one are gone. */
int g_iContextGeneration = 0;

/* An element buffer holding a repeating index pattern, grown when a draw
 * needs more pieces than it has. */
struct PatternIndices {
  GLuint ibo = 0;
  int pieces = 0;
};

/* GPU buffers for immediate (sprite) draws. thread_local like g_cache: each
 * rendering thread has its own context, write offset and buffers. */
struct StreamBuffers {
  int generation = -1;
  // Persistently mapped ring (ARB_buffer_storage), split into quarters with a
  // fence each, so a quarter is only rewritten once the GPU is done with it.
  GLuint ring = 0;
  char* mapped = nullptr;
  GLsizeiptr offset = 0;
  int quarter = 0;
  GLsync fences[4] = {};
  // Without buffer storage, or for draws bigger than a quarter: respecified
  // with glBufferData every draw, which the driver orphans.
  GLuint dynamic = 0;
  PatternIndices quads, quad_strip, symmetric_strip;
};
thread_local StreamBuffers g_stream;
constexpr GLsizeiptr kStreamRingSize = 4 << 20;

void CheckStreamGeneration() {
  if (g_stream.generation == g_iContextGeneration) {
    return;
  }
  // Old names belonged to a destroyed context; don't delete them, they may
  // already name something else.
  g_stream = StreamBuffers();
  g_stream.generation = g_iContextGeneration;
  glGenBuffers(1, &g_stream.dynamic);
  if (GLAD_GL_ARB_buffer_storage && GLAD_GL_ARB_sync) {
    const GLbitfield flags =
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    glGenBuffers(1, &g_stream.ring);
    CacheBindBuffer(GL_ARRAY_BUFFER, g_cache.array_buffer, g_stream.ring);
    glBufferStorage(GL_ARRAY_BUFFER, kStreamRingSize, nullptr, flags);
    g_stream.mapped = static_cast<char*>(
        glMapBufferRange(GL_ARRAY_BUFFER, 0, kStreamRingSize, flags));
  }
}

/* Copies data into a stream buffer, leaves that bound to GL_ARRAY_BUFFER and
 * returns the data's offset in it. */
GLintptr StreamUpload(const void* data, GLsizeiptr size) {
  constexpr GLsizeiptr kQuarter = kStreamRingSize / 4;
  CheckStreamGeneration();
  CountGL(RenderProfiler::kGLVertexSetup);
  if (!g_stream.mapped || size > kQuarter) {
    CacheBindBuffer(GL_ARRAY_BUFFER, g_cache.array_buffer, g_stream.dynamic);
    glBufferData(GL_ARRAY_BUFFER, size, data, GL_STREAM_DRAW);
    return 0;
  }

  // Keep each draw inside one quarter.
  GLsizeiptr at = g_stream.offset;
  if (at + size > kStreamRingSize) {
    at = 0;
  } else if (at / kQuarter != (at + size - 1) / kQuarter) {
    at = (at / kQuarter + 1) * kQuarter;
  }
  const int quarter = static_cast<int>(at / kQuarter);
  if (quarter != g_stream.quarter) {
    // Fence the quarter we're leaving; wait for the one we're entering. The
    // GPU is normally a frame or two behind; a quarter holds many frames.
    g_stream.fences[g_stream.quarter] =
        glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    g_stream.quarter = quarter;
    if (GLsync& fence = g_stream.fences[quarter]) {
      CountGL(RenderProfiler::kGLGet);
      glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
      glDeleteSync(fence);
      fence = nullptr;
    }
  }
  memcpy(g_stream.mapped + at, data, size);
  g_stream.offset = at + size;
  CacheBindBuffer(GL_ARRAY_BUFFER, g_cache.array_buffer, g_stream.ring);
  return at;
}

/* The fixed-function state the setters record and the shaders implement. It
 * is never "unknown" like g_cache, so draws read it without glGet.
 * thread_local and reset to GL's defaults per context, like g_stream. */
struct FixedFunctionState {
  int generation = -1;
  GLuint program = 0;  // from SetEffectMode / SetCelShaded; 0 for none
  bool tex_enabled[NUM_TextureUnit] = {};
  TextureMode tex_mode[NUM_TextureUnit] = {};  // GL_MODULATE
  bool texgen[NUM_TextureUnit] = {};
  bool alpha_test = false;
  bool lighting = false;
  int lights = 0;  // bit i: GL_LIGHT0 + i enabled
  int active_unit = 0;

  // Shader uniforms. Each group has a serial (from NextUniformSerial) that
  // changes with its value, so programs re-upload only what changed.
  RageMatrix projection, modelview;
  RageMatrix texture[2];  // per texture unit, set while the unit is active
  uint64_t projection_serial = 0, modelview_serial = 0, texture_serial = 0;
  // Front material and light 0 (position in eye space), GL's defaults.
  RageColor emission{0, 0, 0, 1}, ambient{0.2f, 0.2f, 0.2f, 1},
      diffuse{0.8f, 0.8f, 0.8f, 1}, specular{0, 0, 0, 1};
  float shininess = 0;
  RageColor light_ambient{0, 0, 0, 1}, light_diffuse{1, 1, 1, 1},
      light_specular{1, 1, 1, 1};
  float light_position[4] = {0, 0, 1, 0};
  uint64_t lighting_serial = 0;
};
thread_local FixedFunctionState g_ff_state;

/* Serials are global, so they never repeat between threads, whose programs
 * (and uniform values) are shared. */
uint64_t NextUniformSerial() {
  static std::atomic<uint64_t> serial{0};
  return ++serial;
}

FixedFunctionState& FF() {
  if (g_ff_state.generation != g_iContextGeneration) {
    FixedFunctionState& ff = g_ff_state = FixedFunctionState();
    ff.generation = g_iContextGeneration;
    RageMatrixIdentity(&ff.projection);
    RageMatrixIdentity(&ff.modelview);
    RageMatrixIdentity(&ff.texture[0]);
    RageMatrixIdentity(&ff.texture[1]);
    ff.projection_serial = NextUniformSerial();
    ff.modelview_serial = NextUniformSerial();
    ff.texture_serial = NextUniformSerial();
    ff.lighting_serial = NextUniformSerial();
    // Generic attributes default to (0,0,0,1); give Color and Normal the
    // defaults of the built-ins they replace.
    glVertexAttrib4f(kAttribColor, 1, 1, 1, 1);
    glVertexAttrib3f(kAttribNormal, 0, 0, 1);
  }
  return g_ff_state;
}

// Copies m into dst, with a new serial if it changed.
void SetUniformMatrix(RageMatrix& dst, uint64_t& serial, const RageMatrix& m) {
  if (memcmp(&dst, &m, sizeof(RageMatrix)) != 0) {
    dst = m;
    serial = NextUniformSerial();
  }
}

/* gl_NormalMatrix: the inverse transpose of the modelview's upper 3x3, which
 * is its cofactor matrix over the determinant. out is column-major. */
void GetNormalMatrix(const RageMatrix& m, float out[9]) {
  float a[3][3];  // a[row][col] of the GL matrix; RageMatrix is transposed
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      a[r][c] = m.m[c][r];
    }
  }
  float cof[3][3];
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      cof[r][c] = a[(r + 1) % 3][(c + 1) % 3] * a[(r + 2) % 3][(c + 2) % 3] -
                  a[(r + 1) % 3][(c + 2) % 3] * a[(r + 2) % 3][(c + 1) % 3];
    }
  }
  const float det =
      a[0][0] * cof[0][0] + a[0][1] * cof[0][1] + a[0][2] * cof[0][2];
  const float inv = det != 0 ? 1 / det : 1;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out[c * 3 + r] = cof[r][c] * inv;
    }
  }
}

/* Binds p, first regrowing it to cover iPieces pieces of N indices each;
 * piece(i, out) writes the N indices of piece i. */
template <int N, class Piece>
void BindPatternIndices(PatternIndices& p, int iPieces, Piece piece) {
  CheckStreamGeneration();
  if (!p.ibo) {
    glGenBuffers(1, &p.ibo);
  }
  CacheBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_cache.element_buffer, p.ibo);
  if (iPieces <= p.pieces) {
    return;
  }
  std::vector<GLuint> vIndices(size_t(iPieces) * N);
  for (int i = 0; i < iPieces; ++i) {
    piece(GLuint(i), &vIndices[size_t(i) * N]);
  }
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER, vIndices.size() * sizeof(GLuint),
      vIndices.data(), GL_STATIC_DRAW);
  p.pieces = iPieces;
}
}  // namespace

RageDisplay_Legacy::RageDisplay_Legacy() {
  LOG->Trace("RageDisplay_Legacy::RageDisplay_Legacy()");
  LOG->MapLog("renderer", "Current renderer: OpenGL");

  FixLittleEndian();
  RageDisplay_Legacy_Helpers::Init();

  g_pWind = nullptr;
  offscreenRenderTarget = nullptr;
}

// Info log of a shader, or of a program if bProgram.
std::string GetInfoLog(GLuint h, bool bProgram) {
  GLint iLength = 0;
  if (bProgram) {
    glGetProgramiv(h, GL_INFO_LOG_LENGTH, &iLength);
  } else {
    glGetShaderiv(h, GL_INFO_LOG_LENGTH, &iLength);
  }
  if (!iLength) {
    return std::string();
  }

  std::string sRet(iLength, '\0');
  if (bProgram) {
    glGetProgramInfoLog(h, iLength, &iLength, &sRet[0]);
  } else {
    glGetShaderInfoLog(h, iLength, &iLength, &sRet[0]);
  }
  sRet.resize(iLength);
  TrimRight(sRet);
  return sRet;
}

/* The shaders are written for GLSL 3.30. Drivers without it (macOS's 2.1
 * compatibility context) get GLSL 1.20 and macros for the 3.30 names. */
static bool g_bGLSL330 = false;

static const char* ShaderPreamble(GLenum ShaderType) {
  if (g_bGLSL330) {
    return ShaderType == GL_FRAGMENT_SHADER
               ? "#version 330\nout vec4 FragColor;\n"
               : "#version 330\n";
  }
  return ShaderType == GL_FRAGMENT_SHADER
             ? "#version 120\n"
               "#define in varying\n"
               "#define FragColor gl_FragColor\n"
               "#define texture texture2D\n"
               "#define textureProj texture2DProj\n"
             : "#version 120\n"
               "#define in attribute\n"
               "#define out varying\n";
}

GLuint CompileShader(
    GLenum ShaderType, std::string sFile, std::vector<std::string> asDefines) {
  std::string sBuffer;
  {
    RageFile file;
    if (!file.Open(sFile)) {
      LOG->Warn(
          "Error compiling shader %s: %s", sFile.c_str(),
          file.GetError().c_str());
      return 0;
    }

    if (file.Read(sBuffer, file.GetFileSize()) == -1) {
      LOG->Warn(
          "Error compiling shader %s: %s", sFile.c_str(),
          file.GetError().c_str());
      return 0;
    }
  }

  LOG->Trace("Compiling shader %s", sFile.c_str());
  GLuint hShader = glCreateShader(ShaderType);
  std::vector<const GLchar*> apData;
  std::vector<GLint> aiLength;
  // #version has to come first.
  apData.push_back(ShaderPreamble(ShaderType));
  aiLength.push_back(strlen(apData.back()));
  for (std::string& s : asDefines) {
    s = ssprintf("#define %s\n", s.c_str());
    apData.push_back(s.data());
    aiLength.push_back(s.size());
  }
  apData.push_back("#line 1\n");
  aiLength.push_back(8);

  apData.push_back(sBuffer.data());
  aiLength.push_back(sBuffer.size());
  glShaderSource(hShader, apData.size(), &apData[0], &aiLength[0]);

  glCompileShader(hShader);

  std::string sInfo = GetInfoLog(hShader, false);

  GLint bCompileStatus = GL_FALSE;
  glGetShaderiv(hShader, GL_COMPILE_STATUS, &bCompileStatus);
  if (!bCompileStatus) {
    LOG->Warn("Error compiling shader %s:\n%s", sFile.c_str(), sInfo.c_str());
    glDeleteShader(hShader);
    return 0;
  }

  if (!sInfo.empty()) {
    LOG->Trace(
        "Messages compiling shader %s:\n%s", sFile.c_str(), sInfo.c_str());
  }

  return hShader;
}

/* Links Data/Shaders/GLSL/<sVert> and <sFrag> into a program; 0 on failure. */
GLuint LoadProgram(const std::string& sVert, const std::string& sFrag) {
  const std::string sDir = "Data/Shaders/GLSL/";
  GLuint hVert = CompileShader(GL_VERTEX_SHADER, sDir + sVert, {});
  GLuint hFrag = CompileShader(GL_FRAGMENT_SHADER, sDir + sFrag, {});
  GLuint hProgram = 0;
  if (hVert && hFrag) {
    hProgram = glCreateProgram();
    glAttachShader(hProgram, hVert);
    glAttachShader(hProgram, hFrag);
    glBindAttribLocation(hProgram, kAttribPosition, "Position");
    glBindAttribLocation(hProgram, kAttribColor, "Color");
    glBindAttribLocation(hProgram, kAttribTexCoord, "TexCoord");
    glBindAttribLocation(hProgram, kAttribNormal, "Normal");
    glBindAttribLocation(
        hProgram, kAttribTextureMatrixScale, "TextureMatrixScale");
    glLinkProgram(hProgram);
    GLint bLinkStatus = GL_FALSE;
    glGetProgramiv(hProgram, GL_LINK_STATUS, &bLinkStatus);
    if (!bLinkStatus) {
      LOG->Warn(
          "Error linking shaders %s and %s: %s", sVert.c_str(), sFrag.c_str(),
          GetInfoLog(hProgram, true).c_str());
      glDeleteProgram(hProgram);
      hProgram = 0;
    }
  }
  // Attached shaders are only flagged for deletion until the program goes.
  glDeleteShader(hVert);
  glDeleteShader(hFrag);
  return hProgram;
}

static GLuint g_bUnpremultiplyShader = 0;
static GLuint g_bColorBurnShader = 0;
static GLuint g_bColorDodgeShader = 0;
static GLuint g_bVividLightShader = 0;
static GLuint g_hHardMixShader = 0;
static GLuint g_hOverlayShader = 0;
static GLuint g_hScreenShader = 0;
static GLuint g_hYUYV422Shader = 0;
static GLuint g_gShellShader = 0;
static GLuint g_gCelShader = 0;
static GLuint g_gDistanceFieldShader = 0;

/* "Fixed function" draws everything no other program is asked for. */
static GLuint g_hFixedFunctionShader = 0;

/* Uniform locations of each program (-1 where it has none), and what was
 * last uploaded to it. Program objects and their uniforms are shared between
 * contexts and only one thread renders at a time, so this is global. */
struct ProgramInfo {
  GLint state, modelview, projection, normal, texture;
  GLint emission, ambient, diffuse, specular, shininess;
  GLint light_ambient, light_diffuse, light_specular, light_position;
  bool lit;  // has any material or light uniform
  int last_state[8];
  uint64_t modelview_serial = 0, projection_serial = 0, normal_serial = 0;
  uint64_t texture_serial = 0, lighting_serial = 0;
};
static std::unordered_map<GLuint, ProgramInfo> g_Programs;

/* Loads a program, sets its samplers and records its uniforms. Returns 0 on
 * failure. */
static GLuint InitProgram(const std::string& sVert, const std::string& sFrag) {
  GLuint hProgram = LoadProgram(sVert, sFrag);
  if (hProgram == 0) {
    return 0;
  }
  CacheUseProgram(hProgram);
  auto loc = [hProgram](const char* name) {
    return glGetUniformLocation(hProgram, name);
  };
  glUniform1i(loc("Texture0"), 0);
  glUniform1i(loc("Texture1"), 0);
  glUniform1i(loc("Texture2"), 1);
  ProgramInfo& p = g_Programs[hProgram];
  p.state = loc("State");
  p.modelview = loc("ModelViewMatrix");
  p.projection = loc("ProjectionMatrix");
  p.normal = loc("NormalMatrix");
  p.texture = loc("TextureMatrix");
  p.emission = loc("MaterialEmission");
  p.ambient = loc("MaterialAmbient");
  p.diffuse = loc("MaterialDiffuse");
  p.specular = loc("MaterialSpecular");
  p.shininess = loc("MaterialShininess");
  p.light_ambient = loc("LightAmbient");
  p.light_diffuse = loc("LightDiffuse");
  p.light_specular = loc("LightSpecular");
  p.light_position = loc("LightPosition");
  p.lit = p.ambient != -1 || p.light_position != -1;
  std::fill(std::begin(p.last_state), std::end(p.last_state), -1);
  CacheUseProgram(0);
  return hProgram;
}

/* Returns an error if the shader every draw depends on didn't load. */
static std::string InitShaders() {
  g_Programs.clear();
  int iMajor = 0, iMinor = 0;
  const char* sGLSL =
      reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
  if (sGLSL != nullptr) {
    sscanf(sGLSL, "%d.%d", &iMajor, &iMinor);
  }
  g_bGLSL330 = iMajor * 100 + iMinor >= 330;
  LOG->Info(
      "GLSL %s: compiling shaders as %s", sGLSL ? sGLSL : "?",
      g_bGLSL330 ? "3.30" : "1.20");
  const char* ff = "Fixed function.vert";
  g_hFixedFunctionShader = InitProgram(ff, "Fixed function.frag");

  // these two are for dancing characters
  g_gShellShader = InitProgram("Shell.vert", "Shell.frag");
  g_gCelShader = InitProgram("Cel.vert", "Cel.frag");
  g_gDistanceFieldShader =
      InitProgram("Distance field.vert", "Distance field.frag");

  // effects
  g_bUnpremultiplyShader = InitProgram(ff, "Unpremultiply.frag");
  g_bColorBurnShader = InitProgram(ff, "Color burn.frag");
  g_bColorDodgeShader = InitProgram(ff, "Color dodge.frag");
  g_bVividLightShader = InitProgram(ff, "Vivid light.frag");
  g_hHardMixShader = InitProgram(ff, "Hard mix.frag");
  g_hOverlayShader = InitProgram(ff, "Overlay.frag");
  g_hScreenShader = InitProgram(ff, "Screen.frag");
  g_hYUYV422Shader = InitProgram(ff, "YUYV422.frag");

  if (g_hFixedFunctionShader == 0) {
    return "The \"Fixed function\" shader failed to compile; see the log.";
  }
  return std::string();
}

static LocalizedString OBTAIN_AN_UPDATED_VIDEO_DRIVER(
    "RageDisplay_Legacy",
    "Obtain an updated driver from your video card manufacturer.");
static LocalizedString GLDIRECT_IS_NOT_COMPATIBLE(
    "RageDisplay_Legacy",
    "GLDirect was detected.  GLDirect is not compatible with this game and "
    "should be disabled.");
std::string RageDisplay_Legacy::Init(
    const VideoModeParams& p, bool bAllowUnacceleratedRenderer) {
  g_pWind = LowLevelWindow::Create();

  bool bIgnore = false;
  std::string sError = SetVideoMode(p, bIgnore);
  if (sError != "") {
    return sError;
  }

  // Log driver details
  g_pWind->LogDebugInformation();
  LOG->Info(
      "OGL Vendor: %s", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
  LOG->Info(
      "OGL Renderer: %s",
      reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
  LOG->Info(
      "OGL Version: %s",
      reinterpret_cast<const char*>(glGetString(GL_VERSION)));
  LOG->Info("OGL Max texture size: %i", GetMaxTextureSize());
  LOG->Info("OGL Texture units: %i", g_iMaxTextureUnits);

  /* Pretty-print the extension string: */
  LOG->Info("OGL Extensions:");
  {
    // glGetString(GL_EXTENSIONS) is gone in core profiles; macOS's 2.1
    // compatibility context has no glGetStringi.
    std::vector<std::string> asExtensions;
    if (GLAD_GL_VERSION_3_0) {
      GLint iNumExtensions = 0;
      glGetIntegerv(GL_NUM_EXTENSIONS, &iNumExtensions);
      for (GLint i = 0; i < iNumExtensions; ++i) {
        asExtensions.push_back(
            reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i)));
      }
    } else {
      split(
          reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)), " ",
          asExtensions);
    }
    sort(asExtensions.begin(), asExtensions.end());
    size_t iNextToPrint = 0;
    while (iNextToPrint < asExtensions.size()) {
      size_t iLastToPrint = iNextToPrint;
      std::string sType;
      for (size_t i = iNextToPrint; i < asExtensions.size(); ++i) {
        std::vector<std::string> asBits;
        split(asExtensions[i], "_", asBits);
        std::string sThisType;
        if (asBits.size() > 2) {
          sThisType = join("_", asBits.begin(), asBits.begin() + 2);
        }
        if (i > iNextToPrint && sThisType != sType) {
          break;
        }
        sType = sThisType;
        iLastToPrint = i;
      }

      if (iNextToPrint == iLastToPrint) {
        LOG->Info("  %s", asExtensions[iNextToPrint].c_str());
        ++iNextToPrint;
        continue;
      }

      std::string sList = ssprintf("  %s: ", sType.c_str());
      while (iNextToPrint <= iLastToPrint) {
        std::vector<std::string> asBits;
        split(asExtensions[iNextToPrint], "_", asBits);
        std::string sShortExt = join("_", asBits.begin() + 2, asBits.end());
        sList += sShortExt;
        if (iNextToPrint < iLastToPrint) {
          sList += ", ";
        }
        if (iNextToPrint == iLastToPrint ||
            sList.size() + asExtensions[iNextToPrint + 1].size() > 120) {
          LOG->Info("%s", sList.c_str());
          sList = "    ";
        }
        ++iNextToPrint;
      }
    }
  }

  if (g_pWind->IsSoftwareRenderer(sError)) {
    if (!bAllowUnacceleratedRenderer) {
      return sError + "  " + OBTAIN_AN_UPDATED_VIDEO_DRIVER.GetValue() + "\n\n";
    }
    LOG->Warn("Low-performance OpenGL renderer: %s", sError.c_str());
  }

#if defined(_WIN32)
  /* GLDirect is a Direct3D wrapper for OpenGL.  It's rather buggy; and if in
   * any case GLDirect can successfully render us, we should be able to do so
   * too using Direct3D directly.  (If we can't, it's a bug that we can work
   * around--if GLDirect can do it, so can we!) */
  if (!strncmp((const char*)glGetString(GL_RENDERER), "GLDirect", 8)) {
    return GLDIRECT_IS_NOT_COMPATIBLE.GetValue() + "\n";
  }
#endif

  return std::string();
}

RageDisplay_Legacy::~RageDisplay_Legacy() { delete g_pWind; }

void RageDisplay_Legacy::GetDisplaySpecs(DisplaySpecs& out) const {
  out.clear();
  g_pWind->GetDisplaySpecs(out);
}

static void CheckReversePackedPixels() {
  /* Try to create a texture. */
  FlushGLErrors();
  glTexImage2D(
      GL_PROXY_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_BGRA,
      GL_UNSIGNED_SHORT_1_5_5_5_REV, nullptr);

  const GLenum glError = glGetError();
  if (glError == GL_NO_ERROR) {
    g_bReversePackedPixelsWorks = true;
  } else {
    g_bReversePackedPixelsWorks = false;
    LOG->Info(
        "GL_UNSIGNED_SHORT_1_5_5_5_REV failed (%s), disabled",
        GLToString(glError).c_str());
  }
}

bool SetupExtensions() {
  // Through the window's GetProcAddress, which also covers EGL (SDL on
  // Wayland).
  const int iLoaded = gladLoadGLUserPtr(
      [](void* pWind, const char* sName) {
        return reinterpret_cast<GLADapiproc>(
            static_cast<LowLevelWindow*>(pWind)->GetProcAddress(sName));
      },
      g_pWind);
  if (iLoaded == 0) {
    return false;
  }

  const float fGLVersion = StringToFloat((const char*)glGetString(GL_VERSION));
  g_glVersion = std::lrint(fGLVersion * 10);

  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, (GLint*)&g_iMaxTextureUnits);

  CheckReversePackedPixels();
  return true;
}

bool RageDisplay_Legacy::UseOffscreenRenderTarget() {
  if (!GetActualVideoModeParams().renderOffscreen || !TEXTUREMAN) {
    return false;
  }

  if (!offscreenRenderTarget) {
    RenderTargetParam param;
    param.bWithDepthBuffer = true;
    param.bWithAlpha = true;
    param.bFloat = false;
    param.iWidth = GetActualVideoModeParams().width;
    param.iHeight = GetActualVideoModeParams().height;
    RageTextureID id(
        ssprintf("FullscreenTexture%dx%d", param.iWidth, param.iHeight));
    // See if we have this texture loaded already
    // (not GC'd yet). If it exists and we try to recreate
    // it, we'll get an error
    if (TEXTUREMAN->IsTextureRegistered(id)) {
      offscreenRenderTarget =
          static_cast<RageTextureRenderTarget*>(TEXTUREMAN->LoadTexture(id));
    } else {
      offscreenRenderTarget = new RageTextureRenderTarget(id, param);
      TEXTUREMAN->RegisterTexture(id, offscreenRenderTarget);
    }
  }
  return true;
}

void RageDisplay_Legacy::ResolutionChanged() {
  InvalidateGLStateCache();
  // LOG->Warn( "RageDisplay_Legacy::ResolutionChanged" );

  /* Clear any junk that's in the framebuffer. */
  if (BeginFrame()) {
    EndFrame();
  }

  RageDisplay::ResolutionChanged();

  if (offscreenRenderTarget && TEXTUREMAN) {
    TEXTUREMAN->UnloadTexture(offscreenRenderTarget);
    offscreenRenderTarget = nullptr;
  }
}

// Return true if mode change was successful.
// bNewDeviceOut is set true if a new device was created and textures
// need to be reloaded.
std::string RageDisplay_Legacy::TryVideoMode(
    const VideoModeParams& p, bool& bNewDeviceOut) {
  // LOG->Warn( "RageDisplay_Legacy::TryVideoMode( %d, %d, %d, %d, %d, %d )",
  // p.windowed, p.width, p.height, p.bpp, p.rate, p.vsync );

  std::string err;
  err = g_pWind->TryVideoMode(p, bNewDeviceOut);
  if (err != "") {
    return err;  // failed to set video mode
  }

  /* Now that we've initialized, we can search for extensions.  Do this before
   * InvalidateObjects, since AllocateBuffers needs it. */
  if (!SetupExtensions()) {
    return "Couldn't load the OpenGL functions.";
  }

  // Everything below uses the GL 3.0 names for shaders, buffers and FBOs.
  // macOS compatibility contexts stop at 2.1, but provide the same calls
  // through ARB_framebuffer_object.
  if (!GLAD_GL_VERSION_3_0 &&
      !(GLAD_GL_VERSION_2_1 && GLAD_GL_ARB_framebuffer_object)) {
    return ssprintf(
        "OpenGL 3.0 or newer is required; this driver provides %s.",
        reinterpret_cast<const char*>(glGetString(GL_VERSION)));
  }

  if (bNewDeviceOut) {
    /* We have a new OpenGL context, so we have to tell our textures that
     * their OpenGL texture number is invalid. */
    if (TEXTUREMAN) {
      TEXTUREMAN->InvalidateTextures();
    }

    /* Delete all render targets.  They may have associated resources other than
     * the texture itself. */
    for (std::pair<const uintptr_t, RenderTarget*>& rt : g_mapRenderTargets) {
      delete rt.second;
    }
    g_mapRenderTargets.clear();

    /* Recreate all vertex buffers. */
    ++g_iContextGeneration;
    // The cache describes the old context; InitShaders binds programs.
    InvalidateGLStateCache();
    BindVertexArrayObject();
    InvalidateObjects();

    std::string sError = InitShaders();
    if (!sError.empty()) {
      return sError;
    }
  }

// I'm not sure this is correct -Colby
#if defined(WINDOWS)
  /* Set vsync the Windows way, if we can.  (What other extensions are there
   * to do this, for other archs?) */
  // Drivers only hand out wglSwapIntervalEXT with WGL_EXT_swap_control.
  using SwapIntervalFunc = BOOL(WINAPI*)(int);
  const auto wglSwapIntervalEXT = reinterpret_cast<SwapIntervalFunc>(
      g_pWind->GetProcAddress("wglSwapIntervalEXT"));
  if (wglSwapIntervalEXT != nullptr) {
    wglSwapIntervalEXT(p.vsync);
  } else {
    return std::string(
        "The WGL_EXT_swap_control extension is not supported on your "
        "computer.");
  }
#endif

  ResolutionChanged();

  return std::string();  // successfully set mode
}

// https://stackoverflow.com/q/6594214
// Since glGetIntegerv won't initialize the variable if it fails, account for
// failure. We initialize it to -1, so we know if it failed. Failure here is
// extremely uncommon, but was first reported on a setup using an AMD RX 6600
// series GPU & late 2024 driver.
int RageDisplay_Legacy::GetMaxTextureSize() const {
  GLint size = -1;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size);

  if (size <= 0) {
    LOG->Warn(
        "RageDisplay_Legacy::GetMaxTextureSize: glGetIntegerv %s %d",
        (size == -1) ? "failed" : "returned invalid value", size);
    return 2048;
  }

  return size;
}

// Utils/RenderTest: copy each frame from the back buffer before it's presented
// and take screenshots from that copy. Reading the front buffer afterwards
// returns black under Xvfb with Mesa. Costs a readback every frame.
static Preference<bool> g_bScreenshotBackBuffer("ScreenshotBackBuffer", false);
static RageSurface* g_pLastFrame = nullptr;

// Reads a window color buffer (GL_FRONT or GL_BACK) into image, bottom row
// first. Allocates a new RGBA8 surface if image is null or the wrong size.
static RageSurface* ReadWindowPixels(
    GLenum buffer, RageSurface* image, int width, int height) {
  if (image != nullptr && (image->w != width || image->h != height)) {
    delete image;
    image = nullptr;
  }
  if (image == nullptr) {
    const RageDisplay::RagePixelFormatDesc& desc =
        PIXEL_FORMAT_DESC[RagePixelFormat_RGBA8];
    image = CreateSurface(
        width, height, desc.bpp, desc.masks[0], desc.masks[1], desc.masks[2],
        0);
  }

  DebugFlushGLErrors();
  glReadBuffer(buffer);
  DebugAssertNoGLError();
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
  DebugAssertNoGLError();
  return image;
}

bool RageDisplay_Legacy::BeginFrame() {
  // Anything outside RageDisplay may have touched GL. Texture params live on
  // the texture objects and are only changed through this file, so they are
  // kept (create/delete and context hand-offs reset them).
  g_cache.Invalidate();
  /* We do this in here, rather than ResolutionChanged, or we won't update the
   * viewport for the concurrent rendering context. */
  int fWidth = g_pWind->GetActualVideoModeParams().windowWidth;
  int fHeight = g_pWind->GetActualVideoModeParams().windowHeight;

  glViewport(0, 0, fWidth, fHeight);

  glClearColor(0, 0, 0, 0);
  SetZWrite(true);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  bool beginFrame = RageDisplay::BeginFrame();
  if (beginFrame && UseOffscreenRenderTarget()) {
    offscreenRenderTarget->BeginRenderingTo(false);
  }

  return beginFrame;
}

void RageDisplay_Legacy::EndFrame() {
  if (UseOffscreenRenderTarget()) {
    offscreenRenderTarget->FinishRenderingTo();
    Sprite fullscreenSprite;
    // We've got a hold of this, don't want sprite deleting it when
    // it's deleted
    offscreenRenderTarget->m_iRefCount++;
    fullscreenSprite.SetTexture(offscreenRenderTarget);
    fullscreenSprite.SetHorizAlign(align_left);
    fullscreenSprite.SetVertAlign(align_top);
    CameraPushMatrix();
    LoadMenuPerspective(
        0, GetActualVideoModeParams().width, GetActualVideoModeParams().height,
        static_cast<float>(GetActualVideoModeParams().width) / 2.f,
        static_cast<float>(GetActualVideoModeParams().height) / 2.f);
    fullscreenSprite.Draw();
    CameraPopMatrix();
  }

  if (g_bScreenshotBackBuffer && !UseOffscreenRenderTarget()) {
    g_pLastFrame = ReadWindowPixels(
        GL_BACK, g_pLastFrame, GetActualVideoModeParams().width,
        GetActualVideoModeParams().height);
  }

  FrameLimitBeforeVsync();
  g_pWind->SwapBuffers();
  FrameLimitAfterVsync();

  // Some would advise against glFinish(), ever. Those people don't realize
  // the degree of freedom GL hosts are permitted in queueing commands.
  // If left to its own devices, the host could lag behind several frames' worth
  // of commands.
  // glFlush() only forces the host to not wait to execute all commands
  // sent so far; it does NOT block on those commands until they finish.
  // glFinish() blocks. We WANT to block. Why? This puts the engine state
  // reflected by the next frame as close as possible to the on-screen
  // appearance of that frame.
  glFinish();

  g_pWind->Update();

  RageDisplay::EndFrame();
}

RageSurface* RageDisplay_Legacy::CreateScreenshot() {
  int width = g_pWind->GetActualVideoModeParams().width;
  int height = g_pWind->GetActualVideoModeParams().height;

  RageSurface* image = nullptr;
  if (offscreenRenderTarget) {
    RageSurface* raw = GetTexture(offscreenRenderTarget->GetTexHandle());
    image = CreateSurface(
        offscreenRenderTarget->GetImageWidth(),
        offscreenRenderTarget->GetImageHeight(), raw->fmt.BitsPerPixel,
        raw->fmt.Rmask, raw->fmt.Gmask, raw->fmt.Bmask, raw->fmt.Amask);
    RageSurfaceUtils::Blit(raw, image);
    delete raw;
  } else if (g_bScreenshotBackBuffer && g_pLastFrame != nullptr) {
    image = g_pLastFrame;  // the caller owns it; EndFrame makes a new one
    g_pLastFrame = nullptr;
    RageSurfaceUtils::FlipVertically(image);
  } else {
    // TODO: revisit for MacOS, where backbuffer size can be less than window
    // size
    image = ReadWindowPixels(GL_FRONT, nullptr, width, height);
    RageSurfaceUtils::FlipVertically(image);
  }

  return image;
}

RageSurface* RageDisplay_Legacy::GetTexture(uintptr_t iTexture) {
  if (iTexture == 0) {
    return nullptr;  // XXX
  }

  FlushGLErrors();

  CacheBindTexture(static_cast<GLuint>(iTexture));
  GLint iHeight, iWidth, iAlphaBits;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &iHeight);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &iWidth);
  glGetTexLevelParameteriv(
      GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, &iAlphaBits);
  int iFormat = iAlphaBits ? RagePixelFormat_RGBA8 : RagePixelFormat_RGB8;

  const RagePixelFormatDesc& desc = PIXEL_FORMAT_DESC[iFormat];
  RageSurface* pImage = CreateSurface(
      iWidth, iHeight, desc.bpp, desc.masks[0], desc.masks[1], desc.masks[2],
      desc.masks[3]);

  glGetTexImage(
      GL_TEXTURE_2D, 0, g_GLPixFmtInfo[iFormat].format, GL_UNSIGNED_BYTE,
      pImage->pixels);
  AssertNoGLError();

  return pImage;
}

ActualVideoModeParams RageDisplay_Legacy::GetActualVideoModeParams() const {
  return g_pWind->GetActualVideoModeParams();
}

// Points the vertex attributes at sprite vertices in the bound buffer.
static void SetVertexArrays(
    const void* pos, const void* color, GLint color_size, const void* tex,
    const void* normal, GLsizei stride) {
  CountGL(RenderProfiler::kGLVertexSetup, 4);
  CacheAttribArray(kAttribPosition, true);
  glVertexAttribPointer(kAttribPosition, 3, GL_FLOAT, GL_FALSE, stride, pos);
  CacheAttribArray(kAttribColor, true);
  glVertexAttribPointer(
      kAttribColor, color_size, GL_UNSIGNED_BYTE, GL_TRUE, stride, color);
  CacheAttribArray(kAttribTexCoord, true);
  glVertexAttribPointer(kAttribTexCoord, 2, GL_FLOAT, GL_FALSE, stride, tex);
  CacheAttribArray(kAttribNormal, true);
  glVertexAttribPointer(kAttribNormal, 3, GL_FLOAT, GL_FALSE, stride, normal);
}

// Uploads v to the stream VBO and points the arrays at it.
static void SetupVertices(const RageSpriteVertex v[], int iNumVerts) {
  static const bool bBGRA =
      !!GLAD_GL_ARB_vertex_array_bgra || !!GLAD_GL_EXT_vertex_array_bgra;
  if (!bBGRA) {
    // No GL_BGRA vertex arrays: upload a copy with red and blue swapped.
    thread_local std::vector<RageSpriteVertex> vSwapped;
    vSwapped.assign(v, v + iNumVerts);
    for (RageSpriteVertex& sv : vSwapped) {
      std::swap(sv.c.r, sv.c.b);
    }
    v = vSwapped.data();
  }

  const GLsizei stride = sizeof(RageSpriteVertex);
  // In a bound buffer, the array "pointers" are byte offsets into it.
  const char* base = reinterpret_cast<const char*>(
      StreamUpload(v, GLsizeiptr(iNumVerts) * stride));
  // GL_BGRA as the size reads RageVColor's b, g, r, a order.
  SetVertexArrays(
      base + offsetof(RageSpriteVertex, p),
      base + offsetof(RageSpriteVertex, c), bBGRA ? GL_BGRA : 4,
      base + offsetof(RageSpriteVertex, t),
      base + offsetof(RageSpriteVertex, n), stride);
}

/* Binds the program for the next draw: the one asked for by SetEffectMode or
 * SetCelShaded, or with none, "Fixed function". Meshes with per-vertex texture
 * matrix scaling always use "Fixed function" (as the old scaling shader
 * overrode any other). Sets State on programs that have it. */
static void UseDrawProgram(bool bTextureMatrixScale = false) {
  FixedFunctionState& ff = FF();
  GLuint program = ff.program;
  if (program == 0 || bTextureMatrixScale) {
    program = g_hFixedFunctionShader;
  }
  CacheUseProgram(program);

  auto it = g_Programs.find(program);
  if (it == g_Programs.end()) {
    return;
  }
  ProgramInfo& p = it->second;

  // State[0]: texture modes of units 1 and 2, alpha test. State[1]: sphere
  // map on units 1 and 2, lighting (2 with light 0 on), texture matrix scale.
  // ponytail: two texture units and light 0, all anything uses. Units 3-4
  // and lights 1-7 are ignored.
  int state[8] = {0, 0, ff.alpha_test, 0, 0, 0, 0, bTextureMatrixScale};
  if (ff.lighting) {
    state[6] = (ff.lights & 1) != 0 ? 2 : 1;
  }
  for (int tu = 0; tu < 2; ++tu) {
    if (!ff.tex_enabled[tu]) {
      continue;
    }
    switch (ff.tex_mode[tu]) {
      case TextureMode_Modulate:
        state[tu] = 1;
        break;
      case TextureMode_Add:
        state[tu] = 2;
        break;
      case TextureMode_Glow:
        state[tu] = 3;
        break;
      default:
        break;
    }
    state[4 + tu] = ff.texgen[tu];
  }
  if (p.state != -1 &&
      !std::equal(
          std::begin(state), std::end(state), std::begin(p.last_state))) {
    CountGL(RenderProfiler::kGLOther);
    glUniform4iv(p.state, 2, state);
    std::copy(std::begin(state), std::end(state), std::begin(p.last_state));
  }

  if (p.projection != -1 && p.projection_serial != ff.projection_serial) {
    CountGL(RenderProfiler::kGLMatrix);
    glUniformMatrix4fv(
        p.projection, 1, GL_FALSE,
        reinterpret_cast<const float*>(&ff.projection));
    p.projection_serial = ff.projection_serial;
  }
  if (p.modelview != -1 && p.modelview_serial != ff.modelview_serial) {
    CountGL(RenderProfiler::kGLMatrix);
    glUniformMatrix4fv(
        p.modelview, 1, GL_FALSE,
        reinterpret_cast<const float*>(&ff.modelview));
    p.modelview_serial = ff.modelview_serial;
  }
  if (p.texture != -1 && p.texture_serial != ff.texture_serial) {
    CountGL(RenderProfiler::kGLMatrix);
    glUniformMatrix4fv(
        p.texture, 2, GL_FALSE, reinterpret_cast<const float*>(ff.texture));
    p.texture_serial = ff.texture_serial;
  }
  // The normal matrix costs an inverse, so only when lighting or texgen use it.
  const bool bNormals = state[4] || state[5] || state[6];
  if (p.normal != -1 && bNormals && p.normal_serial != ff.modelview_serial) {
    float normal[9];
    GetNormalMatrix(ff.modelview, normal);
    CountGL(RenderProfiler::kGLMatrix);
    glUniformMatrix3fv(p.normal, 1, GL_FALSE, normal);
    p.normal_serial = ff.modelview_serial;
  }
  // "Fixed function" programs only light when lighting is on; others (cel
  // shading) always use the light.
  const bool bLighting = p.state == -1 || state[6] != 0;
  if (p.lit && bLighting && p.lighting_serial != ff.lighting_serial) {
    CountGL(RenderProfiler::kGLLighting, 9);
    glUniform4fv(p.emission, 1, ff.emission);
    glUniform4fv(p.ambient, 1, ff.ambient);
    glUniform4fv(p.diffuse, 1, ff.diffuse);
    glUniform4fv(p.specular, 1, ff.specular);
    glUniform1f(p.shininess, ff.shininess);
    glUniform4fv(p.light_ambient, 1, ff.light_ambient);
    glUniform4fv(p.light_diffuse, 1, ff.light_diffuse);
    glUniform4fv(p.light_specular, 1, ff.light_specular);
    glUniform4fv(p.light_position, 1, ff.light_position);
    p.lighting_serial = ff.lighting_serial;
  }
}

void RageDisplay_Legacy::SendCurrentMatrices() {
  FixedFunctionState& ff = FF();

  RageMatrix projection;
  RageMatrixMultiply(&projection, GetCentering(), GetProjectionTop());
  if (g_bInvertY) {
    RageMatrix flip;
    RageMatrixScale(&flip, +1, -1, +1);
    RageMatrixMultiply(&projection, &flip, &projection);
  }
  SetUniformMatrix(ff.projection, ff.projection_serial, projection);

  // OpenGL has just "modelView", whereas D3D has "world" and "view"
  RageMatrix modelView;
  RageMatrixMultiply(&modelView, GetViewTop(), GetWorldTop());
  SetUniformMatrix(ff.modelview, ff.modelview_serial, modelView);

  // Like GL's texture matrix, this belongs to the active texture unit.
  if (ff.active_unit < 2) {
    SetUniformMatrix(
        ff.texture[ff.active_unit], ff.texture_serial, *GetTextureTop());
  }

  UseDrawProgram();
}

// CPU-side copy of the mesh data that RageCompiledGeometryHWOGL uploads.
class RageCompiledGeometryArrays : public RageCompiledGeometry {
 public:
  void Allocate(const std::vector<msMesh>& vMeshes) {
    /* Always allocate at least 1 entry, so &x[0] is valid. */
    const unsigned int verticesCount =
        std::max<unsigned int>(1u, GetTotalVertices());
    const unsigned int trianglesCount =
        std::max<unsigned int>(1u, GetTotalTriangles());

    m_vPosition.resize(verticesCount);
    m_vTexture.resize(verticesCount);
    m_vNormal.resize(verticesCount);
    m_vTexMatrixScale.resize(verticesCount);
    m_vTriangles.resize(trianglesCount);
  }
  void Change(const std::vector<msMesh>& vMeshes) {
    for (unsigned i = 0; i < vMeshes.size(); i++) {
      const MeshInfo& meshInfo = m_vMeshInfo[i];
      const msMesh& mesh = vMeshes[i];
      const std::vector<RageModelVertex>& Vertices = mesh.Vertices;
      const std::vector<msTriangle>& Triangles = mesh.Triangles;

      for (unsigned j = 0; j < Vertices.size(); j++) {
        m_vPosition[meshInfo.iVertexStart + j] = Vertices[j].p;
        m_vTexture[meshInfo.iVertexStart + j] = Vertices[j].t;
        m_vNormal[meshInfo.iVertexStart + j] = Vertices[j].n;
        m_vTexMatrixScale[meshInfo.iVertexStart + j] =
            Vertices[j].TextureMatrixScale;
      }

      for (unsigned j = 0; j < Triangles.size(); j++) {
        for (unsigned k = 0; k < 3; k++) {
          int iVertexIndexInVBO =
              meshInfo.iVertexStart + Triangles[j].nVertexIndices[k];
          m_vTriangles[meshInfo.iTriangleStart + j].nVertexIndices[k] =
              (uint16_t)iVertexIndexInVBO;
        }
      }
    }
  }

 protected:
  std::vector<RageVector3> m_vPosition;
  std::vector<RageVector2> m_vTexture;
  std::vector<RageVector3> m_vNormal;
  std::vector<msTriangle> m_vTriangles;
  std::vector<RageVector2> m_vTexMatrixScale;
};

class InvalidateObject;
static std::set<InvalidateObject*> g_InvalidateList;
class InvalidateObject {
 public:
  InvalidateObject() { g_InvalidateList.insert(this); }
  virtual ~InvalidateObject() { g_InvalidateList.erase(this); }
  virtual void Invalidate() = 0;
};

static void InvalidateObjects() {
  for (InvalidateObject* it : g_InvalidateList) {
    it->Invalidate();
  }
}

class RageCompiledGeometryHWOGL : public RageCompiledGeometryArrays,
                                  public InvalidateObject {
 protected:
  // vertex buffer object names
  GLuint m_nPositions;
  GLuint m_nTextureCoords;
  GLuint m_nNormals;
  GLuint m_nTriangles;
  GLuint m_nTextureMatrixScale;

  void AllocateBuffers();
  void UploadData();

 public:
  RageCompiledGeometryHWOGL();
  ~RageCompiledGeometryHWOGL();

  /* This is called when our OpenGL context is invalidated. */
  void Invalidate();

  void Allocate(const std::vector<msMesh>& vMeshes);
  void Change(const std::vector<msMesh>& vMeshes);
  void Draw(int iMeshIndex) const;
};

RageCompiledGeometryHWOGL::RageCompiledGeometryHWOGL() {
  m_nPositions = 0;
  m_nTextureCoords = 0;
  m_nNormals = 0;
  m_nTriangles = 0;
  m_nTextureMatrixScale = 0;

  AllocateBuffers();
}

RageCompiledGeometryHWOGL::~RageCompiledGeometryHWOGL() {
  DebugFlushGLErrors();

  // Deleting a bound buffer unbinds it; buffer ids get reused.
  g_cache.array_buffer = g_cache.element_buffer = -1;
  glDeleteBuffers(1, &m_nPositions);
  DebugAssertNoGLError();
  glDeleteBuffers(1, &m_nTextureCoords);
  DebugAssertNoGLError();
  glDeleteBuffers(1, &m_nNormals);
  DebugAssertNoGLError();
  glDeleteBuffers(1, &m_nTriangles);
  DebugAssertNoGLError();
  glDeleteBuffers(1, &m_nTextureMatrixScale);
  DebugAssertNoGLError();
}

void RageCompiledGeometryHWOGL::AllocateBuffers() {
  DebugFlushGLErrors();

  if (!m_nPositions) {
    glGenBuffers(1, &m_nPositions);
    DebugAssertNoGLError();
  }

  if (!m_nTextureCoords) {
    glGenBuffers(1, &m_nTextureCoords);
    DebugAssertNoGLError();
  }

  if (!m_nNormals) {
    glGenBuffers(1, &m_nNormals);
    DebugAssertNoGLError();
  }

  if (!m_nTriangles) {
    glGenBuffers(1, &m_nTriangles);
    DebugAssertNoGLError();
  }

  if (!m_nTextureMatrixScale) {
    glGenBuffers(1, &m_nTextureMatrixScale);
    DebugAssertNoGLError();
  }
}

void RageCompiledGeometryHWOGL::UploadData() {
  DebugFlushGLErrors();
  // Binds buffers below without the cache.
  g_cache.array_buffer = g_cache.element_buffer = -1;

  glBindBuffer(GL_ARRAY_BUFFER, m_nPositions);
  DebugAssertNoGLError();
  glBufferData(
      GL_ARRAY_BUFFER, GetTotalVertices() * sizeof(RageVector3),
      &m_vPosition[0], GL_STATIC_DRAW);
  DebugAssertNoGLError();

  glBindBuffer(GL_ARRAY_BUFFER, m_nTextureCoords);
  DebugAssertNoGLError();
  glBufferData(
      GL_ARRAY_BUFFER, GetTotalVertices() * sizeof(RageVector2), &m_vTexture[0],
      GL_STATIC_DRAW);
  DebugAssertNoGLError();

  glBindBuffer(GL_ARRAY_BUFFER, m_nNormals);
  DebugAssertNoGLError();
  glBufferData(
      GL_ARRAY_BUFFER, GetTotalVertices() * sizeof(RageVector3), &m_vNormal[0],
      GL_STATIC_DRAW);
  DebugAssertNoGLError();

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_nTriangles);
  DebugAssertNoGLError();
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER, GetTotalTriangles() * sizeof(msTriangle),
      &m_vTriangles[0], GL_STATIC_DRAW);
  DebugAssertNoGLError();

  if (m_bAnyNeedsTextureMatrixScale) {
    glBindBuffer(GL_ARRAY_BUFFER, m_nTextureMatrixScale);
    DebugAssertNoGLError();
    glBufferData(
        GL_ARRAY_BUFFER, GetTotalVertices() * sizeof(RageVector2),
        &m_vTexMatrixScale[0], GL_STATIC_DRAW);
    DebugAssertNoGLError();
  }
}

void RageCompiledGeometryHWOGL::Invalidate() {
  /* Our vertex buffers no longer exist.  Reallocate and reupload. */
  m_nPositions = 0;
  m_nTextureCoords = 0;
  m_nNormals = 0;
  m_nTriangles = 0;
  m_nTextureMatrixScale = 0;
  AllocateBuffers();
  UploadData();
}

void RageCompiledGeometryHWOGL::Allocate(const std::vector<msMesh>& vMeshes) {
  DebugFlushGLErrors();
  // Binds buffers below without the cache.
  g_cache.array_buffer = g_cache.element_buffer = -1;

  RageCompiledGeometryArrays::Allocate(vMeshes);
  glBindBuffer(GL_ARRAY_BUFFER, m_nPositions);
  DebugAssertNoGLError();
  glBufferData(
      GL_ARRAY_BUFFER, GetTotalVertices() * sizeof(RageVector3), nullptr,
      GL_STATIC_DRAW);
  DebugAssertNoGLError();

  glBindBuffer(GL_ARRAY_BUFFER, m_nTextureCoords);
  DebugAssertNoGLError();
  glBufferData(
      GL_ARRAY_BUFFER, GetTotalVertices() * sizeof(RageVector2), nullptr,
      GL_STATIC_DRAW);
  DebugAssertNoGLError();

  glBindBuffer(GL_ARRAY_BUFFER, m_nNormals);
  DebugAssertNoGLError();
  glBufferData(
      GL_ARRAY_BUFFER, GetTotalVertices() * sizeof(RageVector3), nullptr,
      GL_STATIC_DRAW);
  DebugAssertNoGLError();

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_nTriangles);
  DebugAssertNoGLError();
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER, GetTotalTriangles() * sizeof(msTriangle),
      nullptr, GL_STATIC_DRAW);
  DebugAssertNoGLError();

  glBindBuffer(GL_ARRAY_BUFFER, m_nTextureMatrixScale);
  DebugAssertNoGLError();
  glBufferData(
      GL_ARRAY_BUFFER, GetTotalVertices() * sizeof(RageVector2), nullptr,
      GL_STATIC_DRAW);
}

void RageCompiledGeometryHWOGL::Change(const std::vector<msMesh>& vMeshes) {
  RageCompiledGeometryArrays::Change(vMeshes);

  UploadData();
}

void RageCompiledGeometryHWOGL::Draw(int iMeshIndex) const {
  DebugFlushGLErrors();

  const MeshInfo& meshInfo = m_vMeshInfo[iMeshIndex];
  if (!meshInfo.iVertexCount || !meshInfo.iTriangleCount) {
    return;
  }

  CountGL(RenderProfiler::kGLVertexSetup, 2);
  CacheAttribArray(kAttribPosition, true);
  CacheBindBuffer(GL_ARRAY_BUFFER, g_cache.array_buffer, m_nPositions);
  glVertexAttribPointer(kAttribPosition, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

  // No color array: Color is the current value SetMaterial sets.
  CacheAttribArray(kAttribColor, false);

  CacheAttribArray(kAttribTexCoord, true);
  CacheBindBuffer(GL_ARRAY_BUFFER, g_cache.array_buffer, m_nTextureCoords);
  glVertexAttribPointer(kAttribTexCoord, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

  // TRICKY:  Don't bind and send normals unless something uses them: lighting,
  // sphere mapping, or a program of its own (cel shading).
  const FixedFunctionState& ff = FF();
  if (ff.lighting || ff.texgen[0] || ff.texgen[1] || ff.program != 0) {
    CountGL(RenderProfiler::kGLVertexSetup);
    CacheAttribArray(kAttribNormal, true);
    CacheBindBuffer(GL_ARRAY_BUFFER, g_cache.array_buffer, m_nNormals);
    glVertexAttribPointer(kAttribNormal, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
  } else {
    CacheAttribArray(kAttribNormal, false);
  }
  DebugAssertNoGLError();

  if (meshInfo.m_bNeedsTextureMatrixScale) {
    glEnableVertexAttribArray(kAttribTextureMatrixScale);
    CacheBindBuffer(
        GL_ARRAY_BUFFER, g_cache.array_buffer, m_nTextureMatrixScale);
    glVertexAttribPointer(
        kAttribTextureMatrixScale, 2, GL_FLOAT, false, 0, nullptr);
    UseDrawProgram(true);
    DebugAssertNoGLError();
  }

  CacheBindBuffer(
      GL_ELEMENT_ARRAY_BUFFER, g_cache.element_buffer, m_nTriangles);
  DebugAssertNoGLError();

#define BUFFER_OFFSET(o) ((char*)(o))

  glDrawRangeElements(
      GL_TRIANGLES,
      meshInfo.iVertexStart,  // minimum array index contained in indices
      meshInfo.iVertexStart + meshInfo.iVertexCount - 1,
      // maximum array index contained in indices
      meshInfo.iTriangleCount * 3,  // number of elements to be rendered
      GL_UNSIGNED_SHORT,
      BUFFER_OFFSET(meshInfo.iTriangleStart * sizeof(msTriangle)));
  DebugAssertNoGLError();

  if (meshInfo.m_bNeedsTextureMatrixScale) {
    glDisableVertexAttribArray(kAttribTextureMatrixScale);
  }
}

RageCompiledGeometry* RageDisplay_Legacy::CreateCompiledGeometry() {
  return new RageCompiledGeometryHWOGL;
}

void RageDisplay_Legacy::DeleteCompiledGeometry(RageCompiledGeometry* p) {
  delete p;
}

void RageDisplay_Legacy::DrawQuadsInternal(
    const RageSpriteVertex v[], int iNumVerts) {
  SendCurrentMatrices();

  SetupVertices(v, iNumVerts);
  // Two triangles per quad, split along 0-2 like the D3D renderer.
  const int iNumQuads = iNumVerts / 4;
  BindPatternIndices<6>(g_stream.quads, iNumQuads, [](GLuint i, GLuint* o) {
    const GLuint b = i * 4;
    o[0] = b + 0, o[1] = b + 1, o[2] = b + 2;
    o[3] = b + 2, o[4] = b + 3, o[5] = b + 0;
  });
  glDrawElements(GL_TRIANGLES, iNumQuads * 6, GL_UNSIGNED_INT, nullptr);
}

void RageDisplay_Legacy::DrawQuadStripInternal(
    const RageSpriteVertex v[], int iNumVerts) {
  SendCurrentMatrices();

  SetupVertices(v, iNumVerts);
  // Quad i is the polygon (2i, 2i+1, 2i+3, 2i+2), split along its first
  // vertex like GL_QUADS. A plain triangle strip would split along 1-2 (as
  // the D3D renderer does), which interpolates colors differently.
  const int iNumQuads = (iNumVerts - 2) / 2;
  BindPatternIndices<6>(
      g_stream.quad_strip, iNumQuads, [](GLuint i, GLuint* o) {
        const GLuint b = i * 2;
        o[0] = b + 0, o[1] = b + 1, o[2] = b + 3;
        o[3] = b + 0, o[4] = b + 3, o[5] = b + 2;
      });
  glDrawElements(GL_TRIANGLES, iNumQuads * 6, GL_UNSIGNED_INT, nullptr);
}

void RageDisplay_Legacy::DrawSymmetricQuadStripInternal(
    const RageSpriteVertex v[], int iNumVerts) {
  int iNumPieces = (iNumVerts - 3) / 3;
  int iNumTriangles = iNumPieces * 4;
  int iNumIndices = iNumTriangles * 3;

  SendCurrentMatrices();

  SetupVertices(v, iNumVerts);
  BindPatternIndices<12>(
      g_stream.symmetric_strip, iNumPieces, [](GLuint i, GLuint* o) {
        // { 1, 3, 0 } { 1, 4, 3 } { 1, 5, 4 } { 1, 2, 5 }
        const GLuint b = i * 3;
        o[0] = b + 1, o[1] = b + 3, o[2] = b + 0;
        o[3] = b + 1, o[4] = b + 4, o[5] = b + 3;
        o[6] = b + 1, o[7] = b + 5, o[8] = b + 4;
        o[9] = b + 1, o[10] = b + 2, o[11] = b + 5;
      });
  glDrawElements(GL_TRIANGLES, iNumIndices, GL_UNSIGNED_INT, nullptr);
}

void RageDisplay_Legacy::DrawFanInternal(
    const RageSpriteVertex v[], int iNumVerts) {
  SendCurrentMatrices();

  SetupVertices(v, iNumVerts);
  glDrawArrays(GL_TRIANGLE_FAN, 0, iNumVerts);
}

void RageDisplay_Legacy::DrawStripInternal(
    const RageSpriteVertex v[], int iNumVerts) {
  SendCurrentMatrices();

  SetupVertices(v, iNumVerts);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, iNumVerts);
}

void RageDisplay_Legacy::DrawTrianglesInternal(
    const RageSpriteVertex v[], int iNumVerts) {
  SendCurrentMatrices();

  SetupVertices(v, iNumVerts);
  glDrawArrays(GL_TRIANGLES, 0, iNumVerts);
}

void RageDisplay_Legacy::DrawCompiledGeometryInternal(
    const RageCompiledGeometry* p, int iMeshIndex) {
  SendCurrentMatrices();

  p->Draw(iMeshIndex);
}

static bool SetTextureUnit(TextureUnit tu) {
  if ((int)tu > g_iMaxTextureUnits) {
    return false;
  }
  CacheActiveUnit(tu);
  FF().active_unit = tu;
  return true;
}

void RageDisplay_Legacy::ClearAllTextures() {
  FOREACH_ENUM(TextureUnit, i)
  SetTexture(i, 0);

  // HACK:  Reset the active texture to 0.
  // TODO:  Change all texture functions to take a stage number.
  CacheActiveUnit(0);
  FF().active_unit = 0;
}

int RageDisplay_Legacy::GetNumTextureUnits() {
  // ponytail: this returned 1 whenever multitexture was supported (the check
  // was inverted), which keeps Model's multitexture path off. Kept as is so
  // rendering doesn't change; returning g_iMaxTextureUnits would enable it.
  return 1;
}

void RageDisplay_Legacy::SetTexture(TextureUnit tu, uintptr_t iTexture) {
  if (!SetTextureUnit(tu)) {
    return;
  }
  FF().tex_enabled[tu] = iTexture != 0;
  if (iTexture) {
    CacheBindTexture(static_cast<GLuint>(iTexture));
  }
}

void RageDisplay_Legacy::SetTextureMode(TextureUnit tu, TextureMode tm) {
  if (!SetTextureUnit(tu)) {
    return;
  }
  if (tm == TextureMode_Modulate || tm == TextureMode_Add ||
      tm == TextureMode_Glow) {
    FF().tex_mode[tu] = tm;  // anything else leaves the mode as it was
  }
}

void RageDisplay_Legacy::SetTextureFiltering(TextureUnit tu, bool b) {
  // Applies to the texture bound on the active unit (not necessarily tu).
  TexParams* params = nullptr;
  const int unit = g_cache.active_unit;
  if (unit >= 0 && g_cache.bound_tex[unit] >= 0) {
    params = &g_tex_params[static_cast<GLuint>(g_cache.bound_tex[unit])];
    if (params->filter == static_cast<int>(b)) {
      return;
    }
  }

  CountGL(RenderProfiler::kGLTexture, 2);
  glTexParameteri(
      GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, b ? GL_LINEAR : GL_NEAREST);

  GLint iMinFilter;
  if (b) {
    bool mipmapped;
    if (params != nullptr && params->mipmapped >= 0) {
      mipmapped = params->mipmapped == 1;
    } else {
      CountGL(RenderProfiler::kGLGet, 2);
      GLint iWidth1 = -1;
      GLint iWidth2 = -1;
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &iWidth1);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &iWidth2);
      mipmapped = iWidth1 > 1 && iWidth2 != 0;
      if (params != nullptr) {
        params->mipmapped = mipmapped;
      }
    }
    if (mipmapped) {
      /* Mipmaps are enabled. */
      if (g_pWind->GetActualVideoModeParams().bTrilinearFiltering) {
        iMinFilter = GL_LINEAR_MIPMAP_LINEAR;
      } else {
        iMinFilter = GL_LINEAR_MIPMAP_NEAREST;
      }
    } else {
      iMinFilter = GL_LINEAR;
    }
  } else {
    iMinFilter = GL_NEAREST;
  }

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iMinFilter);
  if (params != nullptr) {
    params->filter = b;
  }
}

void RageDisplay_Legacy::SetEffectMode(EffectMode effect) {
  GLuint hShader = 0;
  switch (effect) {
    case EffectMode_Normal:
      hShader = 0;
      break;
    case EffectMode_Unpremultiply:
      hShader = g_bUnpremultiplyShader;
      break;
    case EffectMode_ColorBurn:
      hShader = g_bColorBurnShader;
      break;
    case EffectMode_ColorDodge:
      hShader = g_bColorDodgeShader;
      break;
    case EffectMode_VividLight:
      hShader = g_bVividLightShader;
      break;
    case EffectMode_HardMix:
      hShader = g_hHardMixShader;
      break;
    case EffectMode_Overlay:
      hShader = g_hOverlayShader;
      break;
    case EffectMode_Screen:
      hShader = g_hScreenShader;
      break;
    case EffectMode_YUYV422:
      hShader = g_hYUYV422Shader;
      break;
    case EffectMode_DistanceField:
      hShader = g_gDistanceFieldShader;
    default:
      break;
  }

  // Bound by the next draw. Samplers are set when the program is loaded.
  FF().program = hShader;

  if (effect == EffectMode_YUYV422 && hShader != 0) {
    DebugFlushGLErrors();
    CacheUseProgram(hShader);
    GLint iTextureWidthUniform = glGetUniformLocation(hShader, "TextureWidth");
    GLint iWidth;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &iWidth);
    glUniform1i(iTextureWidthUniform, iWidth);
    DebugAssertNoGLError();
  }
}

bool RageDisplay_Legacy::IsEffectModeSupported(EffectMode effect) {
  switch (effect) {
    case EffectMode_Normal:
      return true;
    case EffectMode_Unpremultiply:
      return g_bUnpremultiplyShader != 0;
    case EffectMode_ColorBurn:
      return g_bColorBurnShader != 0;
    case EffectMode_ColorDodge:
      return g_bColorDodgeShader != 0;
    case EffectMode_VividLight:
      return g_bVividLightShader != 0;
    case EffectMode_HardMix:
      return g_hHardMixShader != 0;
    case EffectMode_Overlay:
      return g_hOverlayShader != 0;
    case EffectMode_Screen:
      return g_hScreenShader != 0;
    case EffectMode_YUYV422:
      return g_hYUYV422Shader != 0;
    case EffectMode_DistanceField:
      return g_gDistanceFieldShader != 0;
    default:
      return false;
  }
}

void RageDisplay_Legacy::SetBlendMode(BlendMode mode) {
  if (g_cache.blend == mode) {
    return;
  }
  g_cache.blend = mode;
  RenderProfiler::CountGL(RenderProfiler::kGLBlend, 3);
  glEnable(GL_BLEND);

  if (mode == BLEND_INVERT_DEST) {
    glBlendEquation(GL_FUNC_SUBTRACT);
  } else if (mode == BLEND_SUBTRACT) {
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
  } else {
    glBlendEquation(GL_FUNC_ADD);
  }

  int iSourceRGB, iDestRGB;
  int iSourceAlpha = GL_ONE, iDestAlpha = GL_ONE_MINUS_SRC_ALPHA;
  switch (mode) {
    case BLEND_NORMAL:
      iSourceRGB = GL_SRC_ALPHA;
      iDestRGB = GL_ONE_MINUS_SRC_ALPHA;
      break;
    case BLEND_ADD:
      iSourceRGB = GL_SRC_ALPHA;
      iDestRGB = GL_ONE;
      break;
    case BLEND_SUBTRACT:
      iSourceRGB = GL_SRC_ALPHA;
      iDestRGB = GL_ONE_MINUS_SRC_ALPHA;
      break;
    case BLEND_MODULATE:
      iSourceRGB = GL_ZERO;
      iDestRGB = GL_SRC_COLOR;
      break;
    case BLEND_COPY_SRC:
      iSourceRGB = GL_ONE;
      iDestRGB = GL_ZERO;
      iSourceAlpha = GL_ONE;
      iDestAlpha = GL_ZERO;
      break;
    case BLEND_ALPHA_MASK:
      iSourceRGB = GL_ZERO;
      iDestRGB = GL_ONE;
      iSourceAlpha = GL_ZERO;
      iDestAlpha = GL_SRC_ALPHA;
      break;
    case BLEND_ALPHA_KNOCK_OUT:
      iSourceRGB = GL_ZERO;
      iDestRGB = GL_ONE;
      iSourceAlpha = GL_ZERO;
      iDestAlpha = GL_ONE_MINUS_SRC_ALPHA;
      break;
    case BLEND_ALPHA_MULTIPLY:
      iSourceRGB = GL_SRC_ALPHA;
      iDestRGB = GL_ZERO;
      break;
    case BLEND_WEIGHTED_MULTIPLY:
      /* output = 2*(dst*src).  0.5,0.5,0.5 is identity; darker colors darken
       * the image, and brighter colors lighten the image. */
      iSourceRGB = GL_DST_COLOR;
      iDestRGB = GL_SRC_COLOR;
      break;
    case BLEND_INVERT_DEST:
      /* out = src - dst.  The source color should almost always be #FFFFFF, to
       * make it "1 - dst". */
      iSourceRGB = GL_ONE;
      iDestRGB = GL_ONE;
      break;
    case BLEND_NO_EFFECT:
      iSourceRGB = GL_ZERO;
      iDestRGB = GL_ONE;
      iSourceAlpha = GL_ZERO;
      iDestAlpha = GL_ONE;
      break;
      DEFAULT_FAIL(mode);
  }

  glBlendFuncSeparate(iSourceRGB, iDestRGB, iSourceAlpha, iDestAlpha);
}

bool RageDisplay_Legacy::IsZWriteEnabled() const {
  if (g_cache.zwrite >= 0) {
    return g_cache.zwrite == 1;
  }
  RenderProfiler::CountGL(RenderProfiler::kGLGet);
  bool a;
  glGetBooleanv(GL_DEPTH_WRITEMASK, (unsigned char*)&a);
  return a;
}

bool RageDisplay_Legacy::IsZTestEnabled() const {
  if (g_cache.ztest >= 0) {
    return g_cache.ztest != ZTEST_OFF;
  }
  RenderProfiler::CountGL(RenderProfiler::kGLGet);
  GLenum a;
  glGetIntegerv(GL_DEPTH_FUNC, (GLint*)&a);
  return a != GL_ALWAYS;
}

void RageDisplay_Legacy::ClearZBuffer() {
  bool write = IsZWriteEnabled();
  SetZWrite(true);
  glClear(GL_DEPTH_BUFFER_BIT);
  SetZWrite(write);
}

void RageDisplay_Legacy::SetZWrite(bool b) {
  if (g_cache.zwrite == static_cast<int>(b)) {
    return;
  }
  g_cache.zwrite = b;
  RenderProfiler::CountGL(RenderProfiler::kGLDepth);
  glDepthMask(b);
}

void RageDisplay_Legacy::SetZBias(float f) {
  float fNear = SCALE(f, 0.0f, 1.0f, 0.05f, 0.0f);
  float fFar = SCALE(f, 0.0f, 1.0f, 1.0f, 0.95f);
  if (g_cache.zbias_valid && g_cache.zbias == f) {
    return;
  }
  g_cache.zbias = f;
  g_cache.zbias_valid = true;
  RenderProfiler::CountGL(RenderProfiler::kGLDepth);

  glDepthRange(fNear, fFar);
}

void RageDisplay_Legacy::SetZTestMode(ZTestMode mode) {
  if (g_cache.ztest == mode) {
    return;
  }
  g_cache.ztest = mode;
  RenderProfiler::CountGL(RenderProfiler::kGLDepth, 2);
  glEnable(GL_DEPTH_TEST);
  switch (mode) {
    case ZTEST_OFF:
      glDepthFunc(GL_ALWAYS);
      break;
    case ZTEST_WRITE_ON_PASS:
      glDepthFunc(GL_LEQUAL);
      break;
    case ZTEST_WRITE_ON_FAIL:
      glDepthFunc(GL_GREATER);
      break;
    default:
      FAIL_M(ssprintf("Invalid ZTestMode: %i", mode));
  }
}

void RageDisplay_Legacy::SetTextureWrapping(TextureUnit tu, bool b) {
  /* This should be per-texture-unit state, but it's per-texture state in
   * OpenGl, so we'll behave incorrectly if the same texture is used in more
   * than one texture unit simultaneously with different wrapping. */
  SetTextureUnit(tu);
  TexParams* params = nullptr;
  const int unit = g_cache.active_unit;
  if (unit >= 0 && g_cache.bound_tex[unit] >= 0) {
    params = &g_tex_params[static_cast<GLuint>(g_cache.bound_tex[unit])];
    if (params->wrap == static_cast<int>(b)) {
      return;
    }
    params->wrap = b;
  }
  RenderProfiler::CountGL(RenderProfiler::kGLTexture, 2);

  GLenum mode = b ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode);
}

void RageDisplay_Legacy::SetMaterial(
    const RageColor& emissive, const RageColor& ambient,
    const RageColor& diffuse, const RageColor& specular, float shininess) {
  // TRICKY:  If lighting is off, then setting the material
  // will have no effect.  Even if lighting is off, we still
  // want Models to have basic color and transparency.
  // We can do this fake lighting by setting the vertex color.
  // XXX: unintended: SetLighting must be called before SetMaterial
  FixedFunctionState& ff = FF();
  if (ff.lighting) {
    ff.emission = emissive;
    ff.ambient = ambient;
    ff.diffuse = diffuse;
    ff.specular = specular;
    ff.shininess = shininess;
    ff.lighting_serial = NextUniformSerial();
  } else {
    RageColor c = diffuse;
    c.r += emissive.r + ambient.r;
    c.g += emissive.g + ambient.g;
    c.b += emissive.b + ambient.b;
    RenderProfiler::CountGL(RenderProfiler::kGLLighting);
    glVertexAttrib4fv(kAttribColor, c);
  }
}

void RageDisplay_Legacy::SetLighting(bool b) { FF().lighting = b; }

void RageDisplay_Legacy::SetLightOff(int index) {
  FF().lights &= ~(1 << index);
}

void RageDisplay_Legacy::SetLightDirectional(
    int index, const RageColor& ambient, const RageColor& diffuse,
    const RageColor& specular, const RageVector3& dir) {
  FixedFunctionState& ff = FF();
  ff.lights |= 1 << index;
  if (index != 0) {
    return;  // only light 0 reaches the shaders
  }
  ff.light_ambient = ambient;
  ff.light_diffuse = diffuse;
  ff.light_specular = specular;
  // Like glLightfv(GL_POSITION), the direction is taken to eye space by the
  // modelview of the last draw. RageMatrix is GL's matrix transposed.
  const RageMatrix& m = ff.modelview;
  for (int r = 0; r < 3; ++r) {
    ff.light_position[r] =
        m.m[0][r] * dir.x + m.m[1][r] * dir.y + m.m[2][r] * dir.z;
  }
  ff.light_position[3] = 0;
  ff.lighting_serial = NextUniformSerial();
}

void RageDisplay_Legacy::SetCullMode(CullMode mode) {
  if (g_cache.cull == mode) {
    return;
  }
  g_cache.cull = mode;
  RenderProfiler::CountGL(RenderProfiler::kGLCull, mode == CULL_NONE ? 1 : 2);
  if (mode != CULL_NONE) {
    glEnable(GL_CULL_FACE);
  }
  switch (mode) {
    case CULL_BACK:
      glCullFace(GL_BACK);
      break;
    case CULL_FRONT:
      glCullFace(GL_FRONT);
      break;
    case CULL_NONE:
      glDisable(GL_CULL_FACE);
      break;
    default:
      FAIL_M(ssprintf("Invalid CullMode: %i", mode));
  }
}

const RageDisplay::RagePixelFormatDesc* RageDisplay_Legacy::GetPixelFormatDesc(
    RagePixelFormat pf) const {
  ASSERT(pf < NUM_RagePixelFormat);
  return &PIXEL_FORMAT_DESC[pf];
}

bool RageDisplay_Legacy::SupportsThreadedRendering() {
  return g_pWind->SupportsThreadedRendering();
}

void RageDisplay_Legacy::BeginConcurrentRenderingMainThread() {
  g_pWind->BeginConcurrentRenderingMainThread();
  InvalidateGLStateCache();
  BindVertexArrayObject();
}

void RageDisplay_Legacy::EndConcurrentRenderingMainThread() {
  g_pWind->EndConcurrentRenderingMainThread();
  InvalidateGLStateCache();
}

void RageDisplay_Legacy::BeginConcurrentRendering() {
  g_pWind->BeginConcurrentRendering();
  InvalidateGLStateCache();
  BindVertexArrayObject();
  RageDisplay::BeginConcurrentRendering();
}

void RageDisplay_Legacy::EndConcurrentRendering() {
  g_pWind->EndConcurrentRendering();
}

void RageDisplay_Legacy::DeleteTexture(uintptr_t iTexture) {
  if (iTexture == 0) {
    return;
  }

  // Deleting a bound texture binds 0 in its place; ids get reused. Render
  // targets delete their texture too.
  const GLuint id = static_cast<GLuint>(iTexture);
  for (int64_t& b : g_cache.bound_tex) {
    if (b == id) {
      b = 0;
    }
  }
  g_tex_params.erase(id);

  if (g_mapRenderTargets.find(iTexture) != g_mapRenderTargets.end()) {
    delete g_mapRenderTargets[iTexture];
    g_mapRenderTargets.erase(iTexture);
    return;
  }

  DebugFlushGLErrors();
  glDeleteTextures(1, reinterpret_cast<GLuint*>(&iTexture));
  DebugAssertNoGLError();
}

RagePixelFormat RageDisplay_Legacy::GetImgPixelFormat(
    RageSurface*& img, bool& bFreeImg, int width, int height) {
  RagePixelFormat pixfmt = FindPixelFormat(
      img->format->BitsPerPixel, img->format->Rmask, img->format->Gmask,
      img->format->Bmask, img->format->Amask);

  // Paletted surfaces are always expanded.
  bool bSupported = img->fmt.BytesPerPixel != 1;
  if (pixfmt == RagePixelFormat_Invalid || !SupportsSurfaceFormat(pixfmt)) {
    bSupported = false;
  }

  if (!bSupported) {
    /* The source is paletted or isn't in a supported, known pixel format.
     * Convert it to RGBA8 ourselves, and let OpenGL convert it to whatever
     * the texture's format is. */
    pixfmt = RagePixelFormat_RGBA8;
    ASSERT(SupportsSurfaceFormat(pixfmt));

    const RagePixelFormatDesc* pfd = DISPLAY->GetPixelFormatDesc(pixfmt);

    RageSurface* imgconv = CreateSurface(
        img->w, img->h, pfd->bpp, pfd->masks[0], pfd->masks[1], pfd->masks[2],
        pfd->masks[3]);
    RageSurfaceUtils::Blit(img, imgconv, width, height);
    img = imgconv;
    bFreeImg = true;
  } else {
    bFreeImg = false;
  }

  return pixfmt;
}

uintptr_t RageDisplay_Legacy::CreateTexture(
    RagePixelFormat pixfmt, RageSurface* pImg, bool bGenerateMipMaps) {
  ASSERT(pixfmt < NUM_RagePixelFormat);
  ASSERT(pixfmt != RagePixelFormat_PAL);  // SupportsTextureFormat says no

  /* Find the pixel format of the surface we've been given. */
  bool bFreeImg;
  RagePixelFormat SurfacePixFmt =
      GetImgPixelFormat(pImg, bFreeImg, pImg->w, pImg->h);
  ASSERT(SurfacePixFmt != RagePixelFormat_Invalid);

  GLenum glTexFormat = g_GLPixFmtInfo[pixfmt].internalfmt;
  GLenum glImageFormat = g_GLPixFmtInfo[SurfacePixFmt].format;
  GLenum glImageType = g_GLPixFmtInfo[SurfacePixFmt].type;

  SetTextureUnit(TextureUnit_1);

  // allocate OpenGL texture resource
  uintptr_t iTexHandle = 0;  // glGenTextures fills only the low 32 bits
  glGenTextures(1, reinterpret_cast<GLuint*>(&iTexHandle));
  g_tex_params.erase(static_cast<GLuint>(iTexHandle));  // ids get reused
  ASSERT(iTexHandle != 0);

  CacheBindTexture(static_cast<GLuint>(iTexHandle));

  if (g_pWind->GetActualVideoModeParams().bAnisotropicFiltering &&
      GLAD_GL_EXT_texture_filter_anisotropic) {
    GLfloat fLargestSupportedAnisotropy;
    glGetFloatv(
        GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &fLargestSupportedAnisotropy);
    glTexParameterf(
        GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
        fLargestSupportedAnisotropy);
  }

  SetTextureFiltering(TextureUnit_1, true);
  SetTextureWrapping(TextureUnit_1, false);

  glPixelStorei(
      GL_UNPACK_ROW_LENGTH, pImg->pitch / pImg->format->BytesPerPixel);

  LOG->Trace(
      "%s (format %s, %ix%i, format %s, type %s, pixfmt %i, imgpixfmt %i)",
      "glTexImage2D", GLToString(glTexFormat).c_str(), pImg->w, pImg->h,
      GLToString(glImageFormat).c_str(), GLToString(glImageType).c_str(),
      pixfmt, SurfacePixFmt);

  DebugFlushGLErrors();

  glTexImage2D(
      GL_TEXTURE_2D, 0, glTexFormat, power_of_two(pImg->w),
      power_of_two(pImg->h), 0, glImageFormat, glImageType, nullptr);
  if (pImg->pixels) {
    glTexSubImage2D(
        GL_TEXTURE_2D, 0, 0, 0, pImg->w, pImg->h, glImageFormat, glImageType,
        pImg->pixels);
  }

  if (bGenerateMipMaps) {
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  DebugAssertNoGLError();

  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glFlush();

  if (bFreeImg) {
    delete pImg;
  }
  // Filtering was set before the mip levels were uploaded; re-learn it.
  g_tex_params.erase(static_cast<GLuint>(iTexHandle));
  return iTexHandle;
}

struct RageTextureLock_OGL : public RageTextureLock, public InvalidateObject {
 public:
  RageTextureLock_OGL() {
    m_iTexHandle = 0;
    m_iBuffer = 0;

    CreateObject();
  }

  ~RageTextureLock_OGL() {
    ASSERT(m_iTexHandle == 0);  // locked!
    glDeleteBuffers(1, &m_iBuffer);
  }

  /* This is called when our OpenGL context is invalidated. */
  void Invalidate() { m_iTexHandle = 0; }

  void Lock(uintptr_t iTexHandle, RageSurface* pSurface) {
    ASSERT(m_iTexHandle == 0);
    ASSERT(pSurface->pixels == nullptr);

    CreateObject();

    m_iTexHandle = iTexHandle;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_iBuffer);

    int iSize = pSurface->h * pSurface->pitch;
    glBufferData(GL_PIXEL_UNPACK_BUFFER, iSize, nullptr, GL_STREAM_DRAW);

    void* pSurfaceMemory = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
    pSurface->pixels = (uint8_t*)pSurfaceMemory;
    pSurface->pixels_owned = false;
  }

  void Unlock(RageSurface* pSurface, bool bChanged) {
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

    pSurface->pixels = (uint8_t*)BUFFER_OFFSET(0);

    if (bChanged) {
      DISPLAY->UpdateTexture(
          m_iTexHandle, pSurface, 0, 0, pSurface->w, pSurface->h);
    }

    pSurface->pixels = nullptr;

    m_iTexHandle = 0;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  }

 private:
  void CreateObject() {
    if (m_iBuffer != 0) {
      return;
    }

    DebugFlushGLErrors();
    glGenBuffers(1, &m_iBuffer);
    DebugAssertNoGLError();
  }

  GLuint m_iBuffer;

  uintptr_t m_iTexHandle;
};

RageTextureLock* RageDisplay_Legacy::CreateTextureLock() {
  return new RageTextureLock_OGL;
}

void RageDisplay_Legacy::UpdateTexture(
    uintptr_t iTexHandle, RageSurface* pImg, int iXOffset, int iYOffset,
    int iWidth, int iHeight) {
  CacheBindTexture(static_cast<GLuint>(iTexHandle));

  bool bFreeImg;
  RagePixelFormat SurfacePixFmt =
      GetImgPixelFormat(pImg, bFreeImg, iWidth, iHeight);

  glPixelStorei(
      GL_UNPACK_ROW_LENGTH, pImg->pitch / pImg->format->BytesPerPixel);

  GLenum glImageFormat = g_GLPixFmtInfo[SurfacePixFmt].format;
  GLenum glImageType = g_GLPixFmtInfo[SurfacePixFmt].type;

  glTexSubImage2D(
      GL_TEXTURE_2D, 0, iXOffset, iYOffset, iWidth, iHeight, glImageFormat,
      glImageType, pImg->pixels);

  /* Must unset PixelStore when we're done! */
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glFlush();

  if (bFreeImg) {
    delete pImg;
  }
}

class RenderTarget_FramebufferObject : public RenderTarget {
 public:
  RenderTarget_FramebufferObject();
  ~RenderTarget_FramebufferObject();
  void Create(
      const RenderTargetParam& param, int& iTextureWidthOut,
      int& iTextureHeightOut);
  uintptr_t GetTexture() const { return m_iTexHandle; }
  void StartRenderingTo();
  void FinishRenderingTo();

  virtual bool InvertY() const { return true; }

 private:
  uintptr_t m_iFrameBufferHandle;
  uintptr_t m_iTexHandle;
  uintptr_t m_iDepthBufferHandle;
};

RenderTarget_FramebufferObject::RenderTarget_FramebufferObject() {
  m_iFrameBufferHandle = 0;
  m_iTexHandle = 0;
  m_iDepthBufferHandle = 0;
}

RenderTarget_FramebufferObject::~RenderTarget_FramebufferObject() {
  if (m_iDepthBufferHandle) {
    glDeleteRenderbuffers(1, reinterpret_cast<GLuint*>(&m_iDepthBufferHandle));
  }
  if (m_iFrameBufferHandle) {
    glDeleteFramebuffers(1, reinterpret_cast<GLuint*>(&m_iFrameBufferHandle));
  }
  if (m_iTexHandle) {
    glDeleteTextures(1, reinterpret_cast<GLuint*>(&m_iTexHandle));
  }
}

void RenderTarget_FramebufferObject::Create(
    const RenderTargetParam& param, int& iTextureWidthOut,
    int& iTextureHeightOut) {
  m_Param = param;

  DebugFlushGLErrors();

  // Allocate OpenGL texture resource
  glGenTextures(1, reinterpret_cast<GLuint*>(&m_iTexHandle));
  ASSERT(m_iTexHandle != 0);

  int iTextureWidth = power_of_two(param.iWidth);
  int iTextureHeight = power_of_two(param.iHeight);

  iTextureWidthOut = iTextureWidth;
  iTextureHeightOut = iTextureHeight;

  CacheBindTexture(static_cast<GLuint>(m_iTexHandle));
  GLenum internalformat;
  GLenum type = param.bWithAlpha ? GL_RGBA : GL_RGB;
  if (param.bFloat && (GLAD_GL_VERSION_3_0 || GLAD_GL_ARB_texture_float)) {
    internalformat = param.bWithAlpha ? GL_RGBA16F : GL_RGB16F;
  } else {
    internalformat = param.bWithAlpha ? GL_RGBA8 : GL_RGB8;
  }

  glTexImage2D(
      GL_TEXTURE_2D, 0, internalformat, iTextureWidth, iTextureHeight, 0, type,
      GL_UNSIGNED_BYTE, nullptr);
  DebugAssertNoGLError();

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  g_tex_params.erase(static_cast<GLuint>(m_iTexHandle));  // set raw above

  /* Create the framebuffer object. */
  glGenFramebuffers(1, reinterpret_cast<GLuint*>(&m_iFrameBufferHandle));
  ASSERT(m_iFrameBufferHandle != 0);

  /* Attach the texture to it. */
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_iFrameBufferHandle));
  glFramebufferTexture2D(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
      static_cast<GLuint>(m_iTexHandle), 0);
  DebugAssertNoGLError();

  /* Attach a depth buffer, if requested. */
  if (param.bWithDepthBuffer) {
    glGenRenderbuffers(1, reinterpret_cast<GLuint*>(&m_iDepthBufferHandle));
    ASSERT(m_iDepthBufferHandle != 0);

    glBindRenderbuffer(
        GL_RENDERBUFFER, static_cast<GLuint>(m_iDepthBufferHandle));
    glRenderbufferStorage(
        GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, iTextureWidth, iTextureHeight);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
        static_cast<GLuint>(m_iDepthBufferHandle));
  }

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    FAIL_M(ssprintf("Framebuffer incomplete: %s", GLToString(status).c_str()));
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget_FramebufferObject::StartRenderingTo() {
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_iFrameBufferHandle));
}

void RenderTarget_FramebufferObject::FinishRenderingTo() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool RageDisplay_Legacy::SupportsRenderToTexture() const { return true; }

bool RageDisplay_Legacy::SupportsFullscreenBorderlessWindow() const {
  // In order to support FSBW, we're going to need the LowLevelWindow
  // implementation to support creating a fullscreen borderless window, and
  // we're going to need RenderToTexture support in order to render in
  // alternative resolutions
  return g_pWind->SupportsFullscreenBorderlessWindow() &&
         SupportsRenderToTexture();
}

uintptr_t RageDisplay_Legacy::CreateRenderTarget(
    const RenderTargetParam& param, int& iTextureWidthOut,
    int& iTextureHeightOut) {
  RenderTarget* pTarget = new RenderTarget_FramebufferObject;

  pTarget->Create(param, iTextureWidthOut, iTextureHeightOut);

  uintptr_t iTexture = pTarget->GetTexture();

  ASSERT(g_mapRenderTargets.find(iTexture) == g_mapRenderTargets.end());
  g_mapRenderTargets[iTexture] = pTarget;
  return iTexture;
}

uintptr_t RageDisplay_Legacy::GetRenderTarget() {
  for (std::map<uintptr_t, RenderTarget*>::const_iterator it =
           g_mapRenderTargets.begin();
       it != g_mapRenderTargets.end(); ++it) {
    if (it->second == g_pCurrentRenderTarget) {
      return it->first;
    }
  }
  return 0;
}

// Targets a render target was started inside of, innermost last (an
// ActorFrameTexture drawn into the offscreen frame of RenderOffscreen).
static std::vector<RenderTarget*> g_vOuterRenderTargets;

void RageDisplay_Legacy::SetRenderTarget(
    uintptr_t iTexture, bool bPreserveTexture) {
  InvalidateGLStateCache();
  RenderTarget* pTarget = nullptr;
  if (iTexture != 0) {
    ASSERT(g_mapRenderTargets.find(iTexture) != g_mapRenderTargets.end());
    pTarget = g_mapRenderTargets[iTexture];
  }

  // Back to the outer target (RageTextureRenderTarget::FinishRenderingTo):
  // undo the inner target's camera push and carry on drawing where the outer
  // one left off, without resetting its state or clearing it.
  if (pTarget != nullptr && !g_vOuterRenderTargets.empty() &&
      g_vOuterRenderTargets.back() == pTarget) {
    g_vOuterRenderTargets.pop_back();
    DISPLAY->CameraPopMatrix();
    g_pCurrentRenderTarget->FinishRenderingTo();
    pTarget->StartRenderingTo();
    g_pCurrentRenderTarget = pTarget;
    glViewport(0, 0, pTarget->GetParam().iWidth, pTarget->GetParam().iHeight);
    g_bInvertY = pTarget->InvertY();
    glFrontFace(g_bInvertY ? GL_CW : GL_CCW);
    return;
  }

  // A target started inside another: keep the outer one's camera push.
  if (pTarget != nullptr && g_pCurrentRenderTarget != nullptr) {
    g_vOuterRenderTargets.push_back(g_pCurrentRenderTarget);
    g_pCurrentRenderTarget->FinishRenderingTo();
    g_pCurrentRenderTarget = nullptr;
  }

  if (iTexture == 0) {
    g_vOuterRenderTargets.clear();
    g_bInvertY = false;
    glFrontFace(GL_CCW);

    /* Pop matrixes affected by SetDefaultRenderStates. */
    DISPLAY->CameraPopMatrix();

    /* Reset the viewport. */
    int fWidth = g_pWind->GetActualVideoModeParams().windowWidth;
    int fHeight = g_pWind->GetActualVideoModeParams().windowHeight;
    glViewport(0, 0, fWidth, fHeight);

    if (g_pCurrentRenderTarget) {
      g_pCurrentRenderTarget->FinishRenderingTo();
    }
    g_pCurrentRenderTarget = nullptr;
    return;
  }

  /* Enable the new render target. */
  pTarget->StartRenderingTo();
  g_pCurrentRenderTarget = pTarget;

  /* Set the viewport to the size of the render target. */
  glViewport(0, 0, pTarget->GetParam().iWidth, pTarget->GetParam().iHeight);

  /* If this render target implementation flips Y, compensate.   Inverting will
   * switch the winding order. */
  g_bInvertY = pTarget->InvertY();
  if (g_bInvertY) {
    glFrontFace(GL_CW);
  }

  /* The render target may be in a different OpenGL context, so re-send
   * state.  Push matrixes affected by SetDefaultRenderStates. */
  DISPLAY->CameraPushMatrix();
  SetDefaultRenderStates();

  /* Clear the texture, if requested.  Always set the associated state, for
   * consistency. */
  glClearColor(0, 0, 0, 0);
  SetZWrite(true);

  /* If bPreserveTexture is false, clear the render target.  Only clear the
   * depth buffer if the target has one; otherwise we're clearing the real depth
   * buffer. */
  if (!bPreserveTexture) {
    int iBit = GL_COLOR_BUFFER_BIT;
    if (pTarget->GetParam().bWithDepthBuffer) {
      iBit |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(iBit);
  }
}

void RageDisplay_Legacy::SetPolygonMode(PolygonMode pm) {
  GLenum m;
  switch (pm) {
    case POLYGON_FILL:
      m = GL_FILL;
      break;
    case POLYGON_LINE:
      m = GL_LINE;
      break;
    default:
      FAIL_M(ssprintf("Invalid PolygonMode: %i", pm));
  }
  glPolygonMode(GL_FRONT_AND_BACK, m);
}

std::string RageDisplay_Legacy::GetTextureDiagnostics(
    uintptr_t iTexture) const {
  /*
          s << (bGenerateMipMaps? "glGenerateMipmap":"glTexImage2D");
          s << "(format " << GLToString(glTexFormat) <<
                          ", " << pImg->w << "x" <<  pImg->h <<
                          ", format " << GLToString(iFormat) <<
                          ", type " << GLToString(glImageType) <<
                          ", pixfmt " << pixfmt <<
                          ", imgpixfmt " << SurfacePixFmt <<
                          ")";
          LOG->Trace( "%s", s.str().c_str() );

  glBindTexture( GL_TEXTURE_2D, iTexture );

          GLint iWidth;
          glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GLenum(GL_TEXTURE_WIDTH),
  (GLint *) &iWidth ); GLint iHeight; glGetTexLevelParameteriv( GL_TEXTURE_2D,
  0, GLenum(GL_TEXTURE_HEIGHT), (GLint *) &iHeight ); GLint iFormat;
          glGetTexLevelParameteriv( GL_TEXTURE_2D, 0,
  GLenum(GL_TEXTURE_INTERNAL_FORMAT), (GLint *) &iFormat );

          GL_CHECK_ERROR( "glGetTexLevelParameteriv(GL_TEXTURE_INTERNAL_FORMAT)"
  ); if (iFormat != glTexFormat)
          {
                  sError = ssprintf( "Expected format %s, got %s instead",
                                  GLToString(glTexFormat).c_str(),
  GLToString(iFormat).c_str() ); break;
          }
*/
  return std::string();
}

// The shaders discard alpha <= 1/256 while this is on.
void RageDisplay_Legacy::SetAlphaTest(bool b) { FF().alpha_test = b; }

/*
 * Although we pair texture formats (eg. GL_RGB8) and surface formats
 * (pairs of eg. GL_RGB8,GL_UNSIGNED_SHORT_5_5_5_1), it's possible for
 * a format to be supported for a texture format but not a surface
 * format.  This is abstracted, so you don't need to know about this
 * as a user calling CreateTexture.
 *
 * One case of this is if packed pixels aren't supported.  We can still
 * use 16-bit color modes, but we have to send it in 32-bit.  Almost
 * everything supports packed pixels.
 *
 * Another case of this is incomplete packed pixels support.  Some
 * implementations neglect GL_UNSIGNED_SHORT_*_REV.
 */
bool RageDisplay_Legacy::SupportsSurfaceFormat(RagePixelFormat pixfmt) {
  switch (g_GLPixFmtInfo[pixfmt].type) {
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
      return GLAD_GL_EXT_bgra && g_bReversePackedPixelsWorks;
    default:
      return true;
  }
}

bool RageDisplay_Legacy::SupportsTextureFormat(
    RagePixelFormat pixfmt, bool bRealtime) {
  /* If we support a pixfmt for texture formats but not for surface formats,
   * then we'll have to convert the texture to a supported surface format before
   * uploading. This is too slow for dynamic textures. */
  if (bRealtime && !SupportsSurfaceFormat(pixfmt)) {
    return false;
  }

  if (pixfmt == RagePixelFormat_PAL) {
    return false;  // expanded to RGBA8 on upload instead
  }
  switch (g_GLPixFmtInfo[pixfmt].format) {
    case GL_BGR:
    case GL_BGRA:
      return !!GLAD_GL_EXT_bgra;
    default:
      return true;
  }
}

bool RageDisplay_Legacy::SupportsPerVertexMatrixScale() { return true; }

void RageDisplay_Legacy::SetSphereEnvironmentMapping(TextureUnit tu, bool b) {
  if (!SetTextureUnit(tu)) {
    return;
  }

  FF().texgen[tu] = b;
}

GLint iCelTexture1, iCelTexture2 = 0;

void RageDisplay_Legacy::SetCelShaded(int stage) {
  // Bound by the next draw.
  switch (stage) {
    case 1:
      FF().program = g_gShellShader;
      break;
    case 2:
      FF().program = g_gCelShader;
      break;
    default:
      FF().program = 0;
      break;
  }
}

/*
 * Copyright (c) 2001-2011 Chris Danford, Glenn Maynard, Colby Klein
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
