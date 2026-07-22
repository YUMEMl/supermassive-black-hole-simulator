# Supermassive Black Hole Simulator

A native Windows visualization of the supermassive black hole TON 618, rendered with OpenGL 3.3 and a real-time Kerr-metric shader.

[Download the latest Windows build](https://github.com/YUMEMl/supermassive-black-hole-simulator/releases/latest)

## Run

Extract the release ZIP and open `Supermassive Black Hole-simulator.exe`. Keep the adjacent `shaders` folder with the executable.

Requirements:

- Windows 10 or Windows 11
- OpenGL 3.3 compatible GPU

Rendering is capped at 20 FPS. There is no FPS overlay and no application icon.

## Recommended PC specifications

| Component | Minimum | Recommended for the 20 FPS target |
| --- | --- | --- |
| OS | Windows 10 64-bit | Windows 11 64-bit |
| CPU | 4-core x64 processor | Modern 6-core processor or better |
| Memory | 8 GB RAM | 16 GB RAM |
| GPU | Dedicated OpenGL 3.3 GPU with 2 GB VRAM | Dedicated GPU with 6 GB VRAM or more |
| Storage | 100 MB available | 200 MB available |

The current build is verified on an NVIDIA GeForce RTX 2080 Ti at the 20 FPS cap. Actual performance depends mainly on GPU fragment-shader throughput and display resolution.

## Controls

- `W` / `S`: zoom in or out
- `A` / `D`: orbit left or right
- `Q` / `E`: change viewing latitude
- Left mouse drag: orbit camera
- `F1`: show or hide the parameter panel
- `Esc`: close

The panel controls mass, dimensionless spin (`a/M`), accretion rate, viewing angle, and simulation time scale. The upper-left readout also shows the event-horizon radius calculated from the selected mass and spin.

## Visual model

- OpenGL 3.3 fullscreen fragment shader
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

- OpenGL 3.3
- GLFW 3.4
- GLM 1.0.3
- Dear ImGui 1.92.8
- stb_image_write

## Build

Install CMake 3.20 or newer and a Windows C++17 compiler, then run:

```powershell
git clone --recurse-submodules https://github.com/YUMEMl/supermassive-black-hole-simulator.git
cd supermassive-black-hole-simulator
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The executable is written to `dist/`. GLFW, GLM, Dear ImGui, and stb are vendored under `third_party/`.
