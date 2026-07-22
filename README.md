# Supermassive Black Hole Simulator

A native Windows visualization of the supermassive black hole TON 618, rendered with OpenGL 3.3 and a real-time Kerr-metric shader.

[Download the latest Windows build](https://github.com/YUMEMl/supermassive-black-hole-simulator/releases/latest)

## Run

Extract the release ZIP and open `Supermassive Black Hole-simulator.exe`. Keep the adjacent `shaders` and `assets` folders with the executable.

Requirements:

- Windows 10 or Windows 11
- OpenGL 3.3 compatible GPU

Rendering is capped at 20 FPS. There is no FPS overlay and no application icon.

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
- Animated filaments using the supplied accretion-disk reference image
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
- stb_image / stb_image_write

The accretion texture is based on user-supplied artwork credited to Adis Resic.

## Build

Install CMake 3.20 or newer and a Windows C++17 compiler, then run:

```powershell
git clone --recurse-submodules https://github.com/YUMEMl/supermassive-black-hole-simulator.git
cd supermassive-black-hole-simulator
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The executable is written to `dist/`. GLFW, GLM, Dear ImGui, and stb are vendored under `third_party/`.
