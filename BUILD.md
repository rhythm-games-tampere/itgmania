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

Release packages on Windows and Linux also use profile-guided optimization (MSVC and GCC only). To do the same locally, configure with `-DSM_PGO=GENERATE` and build, play a few songs with that (slower, instrumented) build or run `python Utils/RenderTest/rendertest.py run --theme "Themes/Simply Love"`, then configure the same build directory with `-DSM_PGO=USE` and build again. The profile is kept in `Build/<preset>/pgo`.

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
