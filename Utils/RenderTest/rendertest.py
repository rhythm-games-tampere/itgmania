#!/usr/bin/env python3
"""Reference screenshots and a perf baseline for the rendering work
(Docs/Devdocs/RenderingModernizationPlan.md, Phase 0).

  rendertest.py run [--out DIR] [--renderer opengl|d3d] [--size WxH] [--no-perf]
                    [--pref KEY=VALUE ...]
  rendertest.py compare BASELINE_DIR NEW_DIR

`run` builds an isolated portable install under Build/RenderTest/root
(junctions to this repo's Program, Data, NoteSkins, Songs and _fallback, plus
the RenderTest theme in this directory), writes a fixed Preferences.ini and
lets the theme drive the game: static scenes, two autoplay gameplay runs and
one full-song run with RenderProfiler on. The game exits by itself. Results
land in DIR (default Build/RenderTest/runs/<time>): one PNG per shot,
perf.json/perf.txt from RenderProfiler and the game log.

Time is frame-locked (ConstantUpdateDeltaSeconds + FrameLockedSongTime), so
two runs of the same build should produce identical images.

`compare` diffs two run directories: per-shot pixel stats (diff images are
written to NEW_DIR/diff) and the perf summary. It exits 1 if any shot differs
by more than --tolerance on more than --max-fraction of its pixels.

Needs Python 3.8+ and Pillow. Close any running ITGmania first.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
WORK = REPO / "Build" / "RenderTest"

PREFS = """[Options]
Theme=RenderTest
Language=en
Windowed=1
DisplayWidth={width}
DisplayHeight={height}
DisplayAspectRatio=1.777778
DisplayColorDepth=32
TextureColorDepth=32
MovieColorDepth=32
Vsync=0
VideoRenderers={renderer}
LastSeenVideoDriver={driver}
ConstantUpdateDeltaSeconds=0.016666668
FrameLockedSongTime=1
AutoPlay=Autoplay
ShowDancingCharacters=Select
RandomBackgroundMode=Off
BGBrightness=0.8
EventMode=1
SoundVolume=0
SoundDrivers=Null
AllowUnacceleratedRenderer=1
ShowStats=0
LogToDisk=1
ScreenshotBackBuffer=1
IgnoredDialogs=MissingThemeElement
AdditionalSongFolders=
AdditionalCourseFolders=

[Game-dance]
Theme=RenderTest
"""


def link(target, path):
    """Directory junction on Windows, symlink elsewhere. Never deleted again:
    shutil.rmtree would follow a junction into the repo."""
    if os.path.lexists(path):
        return
    if os.name == "nt":
        subprocess.run(["cmd", "/c", "mklink", "/J", str(path), str(target)],
                       check=True, stdout=subprocess.DEVNULL)
    else:
        os.symlink(target, path, target_is_directory=True)


def make_character(root):
    """A 'character' made of the vivid arrow model, so gameplay draws a lit
    (and with CelShadeModels, cel-shaded) DancingCharacters model."""
    char = root / "Characters" / "rendertest"
    shutil.rmtree(char, ignore_errors=True)  # a real directory we made
    src = REPO / "NoteSkins" / "dance" / "vivid"
    char.mkdir(parents=True)
    (char / "character.ini").write_text("[Character]\nDisplayName=RenderTest\n")
    # The arrow is ~50 units wide; shrink it and lift it into the camera view
    # (+y is up here). Vertex lines are the only ones with 7 fields.
    lines = []
    for line in (src / "model.txt").read_text().splitlines():
        f = line.split()
        if len(f) == 7:
            x, y, z = (float(v) * 0.2 for v in f[1:4])
            f[1:4] = (f"{x:f}", f"{y + 10:f}", f"{z:f}")
            line = " ".join(f)
        lines.append(line)
    (char / "model.txt").write_text("\n".join(lines) + "\n")
    shutil.copytree(src / "textures", char / "textures")
    for anim in ("Rest", "WarmUp", "Dance"):
        (char / anim).mkdir()
        shutil.copy(char / "model.txt", char / anim / "anim.txt")


def primary_video_driver_name():
    """What the game's GetPrimaryVideoName() returns on Windows."""
    import ctypes
    from ctypes import wintypes

    class DISPLAY_DEVICEA(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("DeviceName", ctypes.c_char * 32),
                    ("DeviceString", ctypes.c_char * 128),
                    ("StateFlags", wintypes.DWORD),
                    ("DeviceID", ctypes.c_char * 128),
                    ("DeviceKey", ctypes.c_char * 128)]

    DISPLAY_DEVICE_PRIMARY_DEVICE = 4
    i = 0
    while True:
        dd = DISPLAY_DEVICEA(cb=ctypes.sizeof(DISPLAY_DEVICEA))
        if not ctypes.windll.user32.EnumDisplayDevicesA(None, i, ctypes.byref(dd), 0):
            return ""
        if dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE:
            return dd.DeviceString.decode(errors="replace").rstrip()
        i += 1


def setup_root(renderer, size, skip_perf, extra_prefs=()):
    root = WORK / "root"
    for d in ("Themes", "Characters", "Save"):
        (root / d).mkdir(parents=True, exist_ok=True)
    for d in ("Data", "NoteSkins", "Songs", "BGAnimations",
              "BackgroundEffects", "BackgroundTransitions"):
        link(REPO / d, root / d)
    link(REPO / "Themes" / "_fallback", root / "Themes" / "_fallback")
    link(REPO / "Characters" / "default", root / "Characters" / "default")
    link(HERE / "Theme", root / "Themes" / "RenderTest")
    if os.name == "nt":
        link(REPO / "Program", root / "Program")
        exe = root / "Program" / "ITGmania.exe"
    elif sys.platform == "darwin":
        # The game uses the folder containing the .app as its root.
        app = root / "ITGmania.app"
        shutil.rmtree(app, ignore_errors=True)  # a real copy, safe to delete
        shutil.copytree(REPO / "ITGmania.app", app, symlinks=True)
        exe = app / "Contents" / "MacOS" / "ITGmania"
    else:
        # The game uses the binary's directory as its root, so copy it.
        exe = root / "itgmania"  # lowercase on Linux
        shutil.copy(REPO / "itgmania", exe)
    make_character(root)
    (root / "Portable.ini").touch()
    for d in ("Logs", "Screenshots", "Cache"):  # real directories, safe to clear
        shutil.rmtree(root / d, ignore_errors=True)
        (root / d).mkdir()
    # The game replaces VideoRenderers, the resolution and other video
    # settings with per-GPU defaults whenever LastSeenVideoDriver isn't the
    # current driver name, so write that name.
    prefs = root / "Save" / "Preferences.ini"
    driver = primary_video_driver_name() if os.name == "nt" else "OpenGL"
    width, height = size.split("x")
    text = PREFS.format(renderer=renderer, driver=driver,
                        width=width, height=height)
    # Extra [Options] lines from --pref go right after the section header.
    extra = "".join(p + "\n" for p in extra_prefs)
    text = text.replace("[Options]\n", "[Options]\n" + extra, 1)
    prefs.write_text(text)
    (root / "Save" / "RenderTest.ini").write_text(
        f"[RenderTest]\nSkipPerf={int(skip_perf)}\n")
    return root, exe


def run(args):
    out = Path(args.out) if args.out else WORK / "runs" / time.strftime("%Y%m%d-%H%M%S")
    root, exe = setup_root(args.renderer, args.size, args.no_perf, args.pref)
    print(f"Running {exe} ...")
    log = root / "Logs" / "log.txt"
    t = time.time()
    proc = subprocess.Popen([str(exe)], cwd=exe.parent)
    exception_seen = None
    while proc.poll() is None:
        time.sleep(1)
        if exception_seen is None and log.exists() and \
                "Exception:" in log.read_text(errors="replace"):
            exception_seen = time.time()
        # After an exception the game shows a dialog and waits for a click.
        if exception_seen and time.time() - exception_seen > 10:
            print("The game threw an exception; killing it.")
            proc.kill()
        elif time.time() - t > args.timeout:
            print(f"ITGmania didn't exit within {args.timeout}s; killing it.")
            proc.kill()
    proc.wait()
    print(f"Game ran {time.time() - t:.0f}s")

    out.mkdir(parents=True, exist_ok=True)
    shots = sorted((root / "Screenshots").glob("*__*.png"))
    for p in shots:
        shutil.copy(p, out / (p.name.split("__")[0] + ".png"))
    profiles = sorted((root / "Logs").glob("RenderProfile-*.json"))
    if profiles:
        shutil.copy(profiles[-1], out / "perf.json")
        shutil.copy(profiles[-1].with_suffix(".txt"), out / "perf.txt")
    text = log.read_text(errors="replace") if log.exists() else ""
    if text:
        shutil.copy(log, out / "log.txt")
        lines = text.splitlines()
        errors = [l for l in lines
                  if re.search(r"(?<!_)\berror\b|Dialog:|ok=false|renderer|"
                               r"OGL |driver", l, re.I)]
        for l in errors[:40]:
            print("log:", l)
        # Exception messages span several lines; print the whole thing.
        for i, l in enumerate(lines):
            if "Exception:" in l:
                print("\n".join(lines[i:i + 30]))
                break

    # The theme logs this after its last step, so it means every step ran.
    done = "RenderTest: done" in text
    failed_shots = text.count("ok=false")
    # A single flat colour means the capture failed (e.g. black under Xvfb).
    from PIL import Image
    for p in sorted(out.glob("*.png")):
        if all(lo == hi for lo, hi in Image.open(p).convert("RGB").getextrema()):
            print(f"{p.name} is one flat colour")
            failed_shots += 1
    print(f"{len(shots)} screenshots ({failed_shots} failed), "
          f"perf {'ok' if profiles else 'skipped' if args.no_perf else 'MISSING'}, "
          f"{'all steps ran' if done else 'DID NOT FINISH'} -> {out}")
    if profiles:
        print_perf(json.loads((out / "perf.json").read_text()))
    return 0 if done and (profiles or args.no_perf) and not failed_shots else 1


PERF_ROWS = [
    ("frames", lambda p: p["frames"]),
    ("draw calls/frame", lambda p: p["draws_per_frame"]),
    ("verts/frame", lambda p: p["verts_per_frame"]),
    ("GL state calls/frame", lambda p: p["gl_calls_per_frame"]["total"]),
    ("CPU work mean ms", lambda p: p["cpu_work_ms"]["mean"]),
    ("CPU work p95 ms", lambda p: p["cpu_work_ms"]["p95"]),
    ("CPU work p99 ms", lambda p: p["cpu_work_ms"]["p99"]),
    ("update mean ms", lambda p: p["update_ms"]["mean"]),
    ("present mean ms", lambda p: p["present_ms"]["mean"]),
    ("frame mean ms", lambda p: p["frame_ms"]["mean"]),
]


def print_perf(a, b=None):
    for name, get in PERF_ROWS:
        if b is None:
            print(f"  {name:22} {get(a):10.3f}")
        else:
            x, y = get(a), get(b)
            pct = (y - x) / x * 100 if x else 0
            print(f"  {name:22} {x:10.3f} {y:10.3f} {pct:+7.1f}%")


def compare(args):
    from PIL import Image, ImageChops

    a, b = Path(args.baseline), Path(args.new)
    names = sorted({p.name for p in a.glob("*.png")} | {p.name for p in b.glob("*.png")})
    diff_dir = b / "diff"
    failed = 0
    print(f"{'shot':40} {'max':>4} {'mean':>7} {'over tol':>9}")
    for n in names:
        if not (a / n).exists() or not (b / n).exists():
            print(f"{n:40} missing in {'baseline' if not (a / n).exists() else 'new'}")
            failed += 1
            continue
        ia = Image.open(a / n).convert("RGB")
        ib = Image.open(b / n).convert("RGB")
        if ia.size != ib.size:
            print(f"{n:40} size {ia.size} vs {ib.size}")
            failed += 1
            continue
        d = ImageChops.difference(ia, ib)
        # Largest channel difference per pixel.
        r, g, bl = d.split()
        hist = ImageChops.lighter(ImageChops.lighter(r, g), bl).histogram()
        total = ia.size[0] * ia.size[1]
        over = sum(hist[args.tolerance + 1:]) / total
        mx = max(i for i, c in enumerate(hist) if c)
        mean = sum(i * c for i, c in enumerate(hist)) / total
        bad = over > args.max_fraction
        failed += bad
        print(f"{n:40} {mx:4d} {mean:7.3f} {over * 100:8.3f}%{'  FAIL' if bad else ''}")
        if mx:
            diff_dir.mkdir(exist_ok=True)
            Image.eval(d, lambda v: min(255, v * 8)).save(diff_dir / n)

    pa, pb = a / "perf.json", b / "perf.json"
    if pa.exists() and pb.exists():
        print(f"\n{'perf':24} {'baseline':>10} {'new':>10}")
        print_perf(json.loads(pa.read_text()), json.loads(pb.read_text()))
    print(f"\n{failed} of {len(names)} shots differ beyond tolerance")
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--out")
    r.add_argument("--renderer", default="opengl")
    r.add_argument("--timeout", type=int, default=900)
    r.add_argument("--size", default="1280x720",
                   help="window size; must fit the desktop (CI uses 960x540)")
    r.add_argument("--no-perf", action="store_true",
                   help="skip the full-song RenderProfiler run")
    r.add_argument("--pref", action="append", default=[], metavar="KEY=VALUE",
                   help="extra [Options] preference, e.g. RenderWithShaders=0")
    c = sub.add_parser("compare")
    c.add_argument("baseline")
    c.add_argument("new")
    c.add_argument("--tolerance", type=int, default=2,
                   help="per-channel difference (0-255) still counted as equal")
    c.add_argument("--max-fraction", type=float, default=0.0,
                   help="fraction of pixels allowed over tolerance")
    args = ap.parse_args()
    sys.exit(run(args) if args.cmd == "run" else compare(args))


if __name__ == "__main__":
    main()
