# Supermassive Black Hole Simulator

[![License: CC0-1.0](https://licensebuttons.net/p/zero/1.0/88x31.png)](https://creativecommons.org/publicdomain/zero/1.0/)
[![Linux Vulkan build](https://github.com/YUMEMl/supermassive-black-hole-simulator/actions/workflows/linux-vulkan-build.yml/badge.svg)](https://github.com/YUMEMl/supermassive-black-hole-simulator/actions/workflows/linux-vulkan-build.yml)
[![Windows build](https://github.com/YUMEMl/supermassive-black-hole-simulator/actions/workflows/windows-build.yml/badge.svg)](https://github.com/YUMEMl/supermassive-black-hole-simulator/actions/workflows/windows-build.yml)

A real-time visualization of the supermassive black hole TON 618, rendered by
integrating photon trajectories through the Kerr metric on the GPU — every pixel
traces its own light ray past the event horizon, photon ring, and accretion disk.

> **Note:** This was put together by an amateur with limited knowledge of
> astronomy, so the calculations are an educational approximation and may not be
> accurate. Please keep that in mind.

<!-- TODO: screenshot / GIF here
![Photon ring and lensed accretion disk](docs/screenshot.png)
-->

## Download

No build required — grab a binary from the
[latest release](https://github.com/YUMEMl/supermassive-black-hole-simulator/releases/latest):

| Platform | File | Renderer |
| --- | --- | --- |
| Windows 10 / 11 (x64) | `Supermassive-Black-Hole-simulator-Windows.zip` | OpenGL 3.3 |
| Linux (x86_64) | `Supermassive-Black-Hole-simulator-Linux-Vulkan-x86_64.tar.gz` | Vulkan 1.1 |

### Run on Windows

Extract the ZIP and open `Supermassive Black Hole-simulator.exe`. Keep the
adjacent `shaders` folder next to the executable.

Because the executable is not code-signed, Windows SmartScreen may show
"Windows protected your PC". Click **More info → Run anyway**. If you prefer not
to trust an unsigned binary, you can [build from source](#build-from-source)
instead.

Requirements: Windows 10/11 (64-bit), OpenGL 3.3 compatible GPU.

### Run on Linux

Extract the tarball and run the executable. Keep the `shaders/*.spv` files next
to it:

```bash
tar -xzf Supermassive-Black-Hole-simulator-Linux-Vulkan-x86_64.tar.gz
cd dist
./"Supermassive Black Hole-simulator"
```

Requirements: 64-bit Linux, a Vulkan 1.1 capable GPU with its Vulkan driver
installed, and a Wayland or X11/XWayland desktop session. GLFW's native Wayland
backend is used automatically when available. OpenGL is not required on Linux.

Virtually all NVIDIA, AMD, and Intel GPUs released within the last decade
support Vulkan 1.1, so it should just work.

## Controls

- `W` / `S`: zoom in or out
- `A` / `D`: orbit left or right
- `Q` / `E`: change viewing latitude
- `P`: toggle continuous auto play and camera orbit
- `Space`: pause or resume the simulation and auto orbit
- Left mouse drag: orbit camera
- `F1`: show or hide the parameter panel
- `Esc`: close

The panel controls mass, dimensionless spin (`a/M`), accretion rate, viewing
angle, and simulation time scale. Auto play keeps the disk animation running
while orbiting the camera until `P` is pressed again. Mass changes the visual
scale and the calculated event-horizon radius shown in the upper-left readout.
The visual scale is deliberately compressed so the full slider range remains
usable on screen.

Rendering is intentionally capped at 20 FPS so the CPU and GPU can idle for the
rest of each frame.

## Visual model

- Numerical photon-path integration (RK4) in a Kerr metric approximation,
  per pixel, with adaptive steps
- Event-horizon silhouette, photon ring, and multiple lensed disk images
- Thin-disk temperature law (`T ∝ r^-3/4`) driving the disk color gradient
- Spin-dependent Doppler-inspired disk asymmetry
- Procedurally generated animated accretion-disk filaments
- Procedural stars and ACES-style tone mapping
- Vulkan/SPIR-V on Linux, OpenGL 3.3 on Windows

This is a real-time educational approximation, not a full
Einstein-field-equation solver. The event horizon is based on the outer Kerr
horizon:

```text
r+ = (GM/c^2) * (1 + sqrt(1 - (a/M)^2))
```

## Recommended PC specifications

| Component | Minimum | Recommended for the 20 FPS target |
| --- | --- | --- |
| OS | 64-bit Linux with Vulkan 1.1, or Windows 10 64-bit | Current 64-bit Linux or Windows 11 |
| CPU | 4-core x64 processor | Modern 6-core processor or better |
| Memory | 8 GB RAM | 16 GB RAM |
| GPU | Vulkan 1.1 or OpenGL 3.3 GPU with 2 GB VRAM | Dedicated GPU with 6 GB VRAM or more |
| Storage | 100 MB available | 200 MB available |

The Vulkan build has been verified on Arch Linux with an NVIDIA GeForce RTX
4070 Ti at the 20 FPS cap. The Windows/OpenGL build was verified on the same GPU
under Windows 11. Actual performance depends mainly on GPU fragment-shader
throughput and display resolution.

## Build from source

Clone with submodules first (GLFW, GLM, Dear ImGui, and stb are vendored under
`third_party/`):

```bash
git clone --recurse-submodules https://github.com/YUMEMl/supermassive-black-hole-simulator.git
cd supermassive-black-hole-simulator
```

The executable is written to `dist/`. Linux builds compile the GLSL shaders into
SPIR-V automatically.

### Arch Linux

Install the compiler, Vulkan shader compiler, Vulkan loader, and GLFW platform
dependencies:

```bash
sudo pacman -S --needed base-devel cmake ninja shaderc vulkan-headers vulkan-icd-loader \
  wayland wayland-protocols libxkbcommon libx11 libxrandr libxinerama libxcursor libxi
```

Also install the Vulkan driver for your GPU, such as `nvidia-utils`,
`vulkan-radeon`, or `vulkan-intel`. Then build and test:

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
```

### Ubuntu / Debian

```bash
sudo apt install build-essential cmake ninja-build glslc libvulkan-dev \
  libwayland-dev wayland-protocols libxkbcommon-dev xorg-dev
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
```

### Windows

Install CMake 3.20 or newer, Ninja, and a Windows C++17 compiler, then run:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Diagnostics mode

Both builds have a built-in diagnostics mode for bug reports and screenshots.
Set the environment variable `TON618_DIAGNOSTICS` before launching, and about
four seconds after startup the simulator writes two files to the system temp
directory:

- `ton618-native-preview.png` — a screenshot of the current frame
- `ton618-native-performance.txt` — FPS, GPU renderer/vendor, graphics API
  version, and camera state

Additional variables select preset scenarios: `TON618_DIAGNOSTICS_ZOOMED_OUT`,
`TON618_DIAGNOSTICS_EDGE_ON`, `TON618_DIAGNOSTICS_LOW_MASS`,
`TON618_DIAGNOSTICS_HIGH_MASS`, and `TON618_DIAGNOSTICS_AUTO_PLAY`. On Linux,
`TON618_DIAGNOSTICS_EXIT` closes the simulator right after the files are
written.

Windows (PowerShell):

```powershell
$env:TON618_DIAGNOSTICS = "1"; $env:TON618_DIAGNOSTICS_EDGE_ON = "1"
& ".\Supermassive Black Hole-simulator.exe"
```

Linux:

```bash
TON618_DIAGNOSTICS=1 TON618_DIAGNOSTICS_EDGE_ON=1 ./"Supermassive Black Hole-simulator"
```

When reporting an issue, attaching both files helps a lot.

## Libraries

- [GLFW](https://github.com/glfw/glfw) 3.4
- [GLM](https://github.com/g-truc/glm) 1.0.3
- [Dear ImGui](https://github.com/ocornut/imgui) 1.92.8
- [stb_image_write](https://github.com/nothings/stb)
- Vulkan (Linux) / OpenGL 3.3 (Windows)

## License

This project is released under [CC0 1.0 Universal](LICENSE) — public domain
dedication. The vendored third-party libraries under `third_party/` keep their
own licenses.
