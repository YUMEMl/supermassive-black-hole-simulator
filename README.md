# Supermassive Black Hole Simulator

Since this was put together on a whim by an amateur with limited knowledge of astronomy, I don't think the calculations are accurate. Please keep that in mind.


A native visualization of the supermassive black hole TON 618. The Windows build uses OpenGL 3.3, while the Linux build uses Vulkan and runs natively on Wayland or X11.

[Download the latest Windows build](https://github.com/YUMEMl/supermassive-black-hole-simulator/releases/latest)

# VULKAN

Since virtually all current major GPUs—as well as those released within the last decade—from NVIDIA, AMD, and Intel support Vulkan, it should work.(It will launch if it supports Vulkan 1.1)

# OpenGL

The simulator will not launch on GPUs or drivers that do not support OpenGL 3.3.

## Run

### Linux (Vulkan)

Build the project, then run:

```bash
./dist/Supermassive\ Black\ Hole-simulator
```

Keep the generated `dist/shaders/*.spv` files next to the executable. A Vulkan-capable GPU and its Vulkan driver are required. The program automatically uses GLFW's native Wayland backend when available and can also run under X11/XWayland.
Requirements:

- 64-bit Linux
- Vulkan 1.1 compatible GPU
- Vulkan loader and a Vulkan driver for your GPU
- Wayland or X11/XWayland desktop session

OpenGL 3.3 is not required for the Linux build.
### Windows (OpenGL)

Extract the release ZIP and open `Supermassive Black Hole-simulator.exe`. Keep the adjacent `shaders` folder with the executable.

Requirements:

- Windows 10 or Windows 11
- OpenGL 3.3 compatible GPU

Rendering is capped at 20 FPS. There is no FPS overlay or application icon.

## Recommended PC specifications

| Component | Minimum | Recommended for the 20 FPS target |
| --- | --- | --- |
| OS | 64-bit Linux with Vulkan 1.1, or Windows 10 64-bit | Current 64-bit Linux or Windows 11 |
| CPU | 4-core x64 processor | Modern 6-core processor or better |
| Memory | 8 GB RAM | 16 GB RAM |
| GPU | Vulkan 1.1 or OpenGL 3.3 GPU with 2 GB VRAM | Dedicated GPU with 6 GB VRAM or more |
| Storage | 100 MB available | 200 MB available |

The Vulkan build has been verified on Arch Linux with an NVIDIA GeForce RTX 4070 ti at the 20 FPS cap. The existing Windows/OpenGL build was previously verified on an NVIDIA GeForce RTX 4070 ti(Windows 11) . Actual performance depends mainly on GPU fragment-shader throughput and display resolution.

## Controls

- `W` / `S`: zoom in or out
- `A` / `D`: orbit left or right
- `Q` / `E`: change viewing latitude
- `P`: toggle continuous auto play and camera orbit
- `Space`: pause or resume the simulation and auto orbit
- Left mouse drag: orbit camera
- `F1`: show or hide the parameter panel
- `Esc`: close

The panel controls mass, dimensionless spin (`a/M`), accretion rate, viewing angle, and simulation time scale. Auto play keeps the disk animation running while orbiting the camera until `P` is pressed again. Mass changes the visual scale and the calculated event-horizon radius shown in the upper-left readout. The visual scale is deliberately compressed so the full slider range remains usable on screen.

## Visual model

- Vulkan/SPIR-V on Linux and OpenGL 3.3 on Windows
- Numerical photon-path integration in a Kerr metric approximation
- Event-horizon silhouette, photon ring, and multiple lensed disk images
- `T proportional to r^-3/4` disk color gradient
- Spin-dependent Doppler-inspired disk asymmetry
- Procedurally generated animated accretion-disk filaments
- Procedural stars and ACES-style tone mapping

The shader integrates photon trajectories per pixel with adaptive steps. It is a real-time educational approximation, not a full Einstein-field-equation solver.

The event horizon is based on the outer Kerr horizon:

```text
r+ = (GM/c^2) * (1 + sqrt(1 - (a/M)^2))
```

## Libraries

- Vulkan on Linux
- OpenGL 3.3 on Windows
- GLFW 3.4
- GLM 1.0.3
- Dear ImGui 1.92.8
- stb_image_write

## Build

Clone the submodules first:

```bash
git clone --recurse-submodules https://github.com/YUMEMl/supermassive-black-hole-simulator.git
cd supermassive-black-hole-simulator
```

### Arch Linux

Install the compiler, Vulkan shader compiler, Vulkan loader, and GLFW platform dependencies:

```bash
sudo pacman -S --needed base-devel cmake ninja shaderc vulkan-headers vulkan-icd-loader \
  wayland wayland-protocols libxkbcommon libx11 libxrandr libxinerama libxcursor libxi
```

Install the Vulkan driver for your GPU as well, such as `nvidia-utils`, `vulkan-radeon`, or `vulkan-intel`. Then build and test:

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
```

### Ubuntu/Debian

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

These codes are licensed under CC0.
CC0
ctest --test-dir build --output-on-failure
```

The executable is written to `dist/`. GLFW, GLM, Dear ImGui, and stb are vendored under `third_party/`. Linux builds compile the GLSL shaders into SPIR-V automatically.
li

# LICENSE
These codes are licensed under CC0.
[![These codes are licensed under CC0.](https://qiita-user-contents.imgix.net/https%3A%2F%2Fmirrors.creativecommons.org%2Fpresskit%2Fbuttons%2F88x31%2Fsvg%2Fcc-zero.svg?ixlib=rb-4.0.0&auto=format&gif-q=60&q=75&w=1400&fit=max&s=034c24fa0b72713fa520808aebed578b)](https://creativecommons.org/publicdomain/zero/1.0/deed.en)
