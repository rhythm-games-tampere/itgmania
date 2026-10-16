# Instructions

Most users will find everything they need in this README. There are differences in the build process for ITGmania as compared to Stepmania, so it is recommended to stick with these ITGmania instructions. For anything not covered in this guide, you can usually refer to the original Stepmania documents [here](https://github.com/stepmania/stepmania/wiki/Compiling-StepMania), as long as you replace references to Stepmania with ITGmania.

## Continuous Integration
Pushes to the `beta` branch of the repository are built with a [GitHub Actions workflow](https://github.com/itgmania/itgmania/actions/workflows/release.yml), in which "nightly releases" are compiled for a matrix of operating systems and architectures. Full releases are built on pushses to the `release` branch.

By default, GitHub stores build artifacts for 90 days. People who are signed into GitHub and have read access to a repository can download workflow artifacts. They can be downloaded from the Artifacts section of the Summary page on an execution of the workflow.

## Local Development

#### Warning

Using CMake is considered stable, but not every single combination is known to work.
Using the defaults as suggested should cause minimal problems.

### Init Submodules

Make sure you initialize the submodules after cloning the repository. This is required.

```sh
git clone --depth=1 https://github.com/itgmania/itgmania.git
cd itgmania
git submodule update --init --recursive
```

After that, you can follow the cmake instructions below to create the build.

### Install CMake

At first, you have to install CMake.

#### All OSes

The common way of installing CMake is to go to [CMake's download page](http://www.cmake.org/download/). The minimum version supported is 3.28. The presets below also need [Ninja](https://ninja-build.org/) (`ninja-build` on Debian/Ubuntu, `brew install ninja` on macOS; Visual Studio ships its own).

If this approach is used, consider using the binary distributions. Most should also provide a friendly GUI interface.

#### Windows

For those that prefer package manager systems, [Chocolatey](https://chocolatey.org/) has a CMake package. Run `choco install cmake` to get the latest stable version.

#### macOS Specific

For those that prefer package manager systems, both [Homebrew](http://brew.sh/) and [MacPorts](https://www.macports.org/) offer CMake as part of their offerings. Run `brew install cmake` or `port install cmake` respectively to get the latest stable version.

#### Linux

There are many package managers available for Linux. Look at your manual for more details. Either that, or utilize the All OS specific approach.

### CMake Usage

The project ships a `CMakePresets.json` with one preset per platform: `windows`, `vs2022`, `linux`, `macos-arm64` and `macos-x64`. Each builds into `Build/<preset>`. From the `itgmania` directory (not `src`):

```sh
cmake --preset linux             # configure
cmake --build --preset linux     # build Release
```

Release and RelWithDebInfo builds use link-time optimization. For another configuration, add `--config Debug` (or `RelWithDebInfo`, `MinSizeRel`) to the build command. Options such as `-DWITH_CLUB_FANTASTIC=ON` go on the configure command.

### Profile-Guided Optimization

Release packages on Windows and Linux are built with profile-guided optimization (PGO): an instrumented build of the game is played first, and the compiler then optimizes for the code paths that session actually used. This takes about 5% off the CPU work per frame. It changes how the code is laid out and inlined, not what it does, and it adds no CPU requirements. PGO is supported with MSVC and GCC; macOS (Clang) packages are built without it.

#### What the release training covers

The release workflow trains on one Simply Love session (menus, gameplay, evaluation) driven by `Utils/RenderTest`, on Mesa's software OpenGL. That covers the game logic, Lua, the note field, the OpenGL renderer, the SDL window, and audio decoding and mixing. It does **not** cover:

- **The Direct3D renderer** (`VideoRenderers=d3d`). This matters most of the items here: rendering runs every frame.
- GPU-vendor-specific OpenGL paths beyond what Mesa reports.
- The sound output drivers (WASAPI, DirectSound, WaveOut, ALSA, PulseAudio, JACK); training uses the Null driver. These are thin layers over the mixing, which is trained.
- Dance pads, other HID and serial input devices, and lights drivers.
- The native (non-SDL) windows (`WindowDriver=Native`).

Code the training never ran still works the same. GCC optimizes it as it would without PGO. MSVC treats it as cold and optimizes it for size, so on Windows those particular paths can run a little slower than in a build without PGO. The game as a whole still comes out ahead, because most of the CPU work is in the trained code: with Direct3D, the release profile still takes about 1.5% off the CPU work per frame, and a build trained on a Direct3D session takes about 4% off. If you run the game with Direct3D, or another configuration from the list matters to you, train your own build as below to get the full benefit.

#### Training your own build

From the `itgmania` directory, using the preset for your platform (`windows` here):

```sh
cmake --preset windows -DSM_PGO=GENERATE   # instrumented build
cmake --build --preset windows
```

Then run that build the way you play. The instrumented game is slower, and it writes its profile when it exits normally (not when it crashes or is killed). Either play a few songs with your own settings, renderer, pads and lights, or let RenderTest drive a session:

```sh
python Utils/RenderTest/rendertest.py run --theme "Themes/Simply Love"
python Utils/RenderTest/rendertest.py run --theme "Themes/Simply Love" --renderer d3d
```

Every session adds to the profile, so you can combine several, for example one with OpenGL and one with Direct3D. Then build with the profile:

```sh
cmake --preset windows -DSM_PGO=USE
cmake --build --preset windows
```

The profile is kept in `Build/<preset>/pgo`. Delete that directory before generating a new profile after code changes; a stale profile only optimizes the code that hasn't changed. `-DSM_PGO=OFF` returns to a normal build.

IDEs that understand presets (Visual Studio, VS Code, CLion) list them directly. The CMake GUI shows them in its preset drop-down.

### Compiling ITGmania

To use ITGmania on your computer, it is first assumed that **CMake** is run (see README.md for more information).
Then, follow the guide based on your operating system.

#### Windows

Everything needed for building on Windows is available via the Visual Studio Installer:
- The "Desktop development with C++" workload (which includes MSVC, C++ ATL, C++ MFC, C++ modules, C++/CLI support),
- The Windows SDK.

Upon launching the Visual Studio Installer, you will be in the _Workloads_ section. Here, you should select "Desktop development with C++".
![image](https://github.com/user-attachments/assets/7ed2370d-c891-4d7b-9ca5-c9ad39ad969c)

The **Windows SDK** is also required, so click the _Individual components_ section. Either use the search, or scroll thru the list until you find the section titled _SDK's, libraries, and frameworks_ to find the Windows SDK.

![image](https://github.com/user-attachments/assets/8cf1b443-bfbe-4c2f-af39-a904324956f5)

Confirm the installation. Then either open the `itgmania` folder in Visual Studio, which picks up the presets, or run `cmake --preset vs2022` and open **Build/vs2022/ITGmania.sln**. Set the build type to **Release** and press the hollow green button (or Ctrl+F5) to begin building. From a *Developer Command Prompt*, `cmake --preset windows` followed by `cmake --build --preset windows` builds with Ninja instead. The executable is written to `Program/`.

![image](https://github.com/user-attachments/assets/f9235e14-bfc8-4f8f-8b30-9706dfb3bcc6)

#### macOS

For macOS, you can either install the full Xcode package or the Xcode Command Line Tools by itself. If you install Xcode from the App Store, it will install everything needed.

When compiling for macOS, the preset differs depending on whether you are compiling for Apple Silicon (`macos-arm64`) or Intel (`macos-x64`). From the `itgmania` directory (not `src`), run:

```sh
cmake --preset macos-arm64
cmake --build --preset macos-arm64
```

After this step is done, the newly built ITGmania.app will be in your `itgmania` directory. To work in Xcode, configure with `cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64` instead.

#### Linux

From the `itgmania` directory (not `src`), run `cmake --preset linux` followed by `cmake --build --preset linux`. The `itgmania` executable will be in the same directory you are in, so you can type `./itgmania` to run the game.

### Installing ITGmania

Installing in this context refers to placing the folders and generated binary in a standard location based on your operating system.
This guide assumes default install locations.
If you want to change the initial location, pass in `-DCMAKE_INSTALL_PREFIX=/new/path/here` when configuring your local setup.

#### Windows

The default installation directory is `C:\Games\ITGmania`.

#### macOS

The `ITGmania.app` package can be copied to `/Applications` and it will work as expected.

#### Linux

After building, run `sudo cmake --install Build/linux --config Release`. The files will be placed in the location specified:
by default, that is now `/usr/local/itgmania`.

### Last Words

With that, you should be good to go.
If there are still questions, view the resources on the parent directory's README.md file.
