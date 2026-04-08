# MapLibre Native + Slint Integration

This project demonstrates the integration of [MapLibre Native](https://github.com/maplibre/maplibre-native) (C++ mapping library) with the [Slint](https://slint.dev/) UI framework for creating interactive map applications.

## Quick Start

For detailed build instructions, see the platform-specific guides:

- **Linux**: [Ubuntu 24.04 Build Guide](docs/build_guides/Linux_Ubuntu_24.md)
- **macOS**: [macOS Build Guide](docs/build_guides/macOS_Apple_Silicon.md)
- **Windows**: [Windows Build Guide](docs/build_guides/Windows_11.md)

## Screenshots

### Linux desktop

[![Image from Gyazo](https://i.gyazo.com/b2b0b9e0af3a2e8f7342d3db6b3c899f.png)](https://gyazo.com/b2b0b9e0af3a2e8f7342d3db6b3c899f)

### macOS Apple Silicon

[![Image from Gyazo](https://i.gyazo.com/d9506d8ed7d5d30a921624bd7a893c88.jpg)](https://gyazo.com/d9506d8ed7d5d30a921624bd7a893c88)

## Prerequisites

- C++20 compatible compiler
- CMake 3.24 or later
- Rust toolchain (required by Slint and wgpu-native)
- Network connectivity for downloading dependencies

For the WebGPU (wgpu-native) backend, LLVM is also required on Windows (for `bindgen`).

## Basic Build Process

```bash
# Clone and prepare
git clone https://github.com/maplibre/maplibre-native-slint.git
cd maplibre-native-slint
git submodule update --init --recursive

# Build (Slint will be automatically downloaded if needed)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run
./build/cpp/maplibre-slint-example
```

See the platform-specific guides for full instructions including vcpkg setup on Windows.

## Project Structure

- `cpp/` - C++ example application (MapLibre Native + Slint integration)
- `rust/` - Rust example application (uses [maplibre-native-rs](https://github.com/maplibre/maplibre-native-rs))
- `vendor/` - MapLibre Native and cpr submodules
- `docs/` - Platform-specific build guides
- `CMakeLists.txt` - Build configuration

## Features

- **WebGPU rendering**: wgpu-native backend supported on macOS (Apple Silicon) and Windows x64
- **Slint UI Integration**: Displays maps within Slint user interfaces
- **Custom File Source**: HTTP-based tile and resource loading
- **Touch/Mouse Interaction**: Interactive map navigation (partial, in progress)
- **Cross-platform**: Supports Linux, macOS, and Windows

## Platform and Backend Support

### Platform Support Matrix

| Platform        | OpenGL/GLES | Metal | WebGPU (wgpu) | CI Status |
|----------------|-------------|-------|----------------|-----------|
| Linux x86_64   | ✅          | ❌    | 🟨*            | ✅        |
| Linux ARM64    | 🟨*         | ❌    | 🟨*            | ❌        |
| Windows x86_64 | ✅          | ❌    | ✅             | ✅        |
| Windows ARM64  | 🟨*         | ❌    | 🟨*            | ❌        |
| macOS ARM64    | ❌          | ✅    | ✅             | ❌        |
| macOS x86_64   | ❌          | 🟨*   | 🟨*            | ❌        |

**Legend:**
- ✅ **Fully Supported**: Tested and working
- 🟨 **Experimental**: Should work but not extensively tested
- ❌ **Not Supported**: Not implemented or not compatible

**Notes:**
- \* Architecture should work but has not been extensively tested
- CI runs on Ubuntu x86_64 (OpenGL/Xvfb) and Windows x64 (WebGPU/wgpu)
- macOS builds target ARM64 only

### Build Configuration Options

```bash
# Linux/Windows with OpenGL (default)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# macOS with Metal backend
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMLN_WITH_METAL=ON -DMLN_WITH_OPENGL=OFF -G Xcode

# Windows or macOS with WebGPU (wgpu-native) backend
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMLN_WITH_WEBGPU=ON -DMLN_WEBGPU_IMPL_WGPU=ON
```

## Rendering Pipeline

The current rendering pipeline involves GPU-to-CPU data transfer:

1. MapLibre Native's `HeadlessFrontend` renders the map to an internal framebuffer via the selected backend (OpenGL/Metal/wgpu).
2. The rendered image is read back to CPU memory as `mbgl::PremultipliedImage`.
3. The pixel data is converted and copied into a `slint::SharedPixelBuffer`.
4. Slint displays the map image within its UI.
5. User interactions from Slint are forwarded to the MapLibre Native map instance.

**Performance note**: GPU-to-CPU readback creates overhead. A future improvement would use `BorrowedOpenGLTexture` or equivalent to share the GPU texture directly with Slint.

## Roadmap

- [ ] Migrate to a pure Rust implementation using [maplibre-native-rs](https://github.com/maplibre/maplibre-native-rs) and the Slint Rust API — enables WebAssembly support ([issue #46](https://github.com/maplibre/maplibre-native-slint/issues/46))
- [ ] Implement GPU-based rendering pipeline to eliminate GPU-to-CPU readback overhead
- [ ] Fully stabilize touch and mouse interactions (zooming, panning, rotation)

## Troubleshooting

### Build Issues

- Ensure all dependencies are installed per the platform guide
- Run `git submodule update --init --recursive` if vendor directories are empty
- On Windows with the WebGPU backend, install LLVM and ensure `libclang.dll` is accessible

### Runtime Issues

- For headless Linux environments, set up Xvfb or equivalent virtual display
- Verify network connectivity for map tile loading

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test the build process
5. Submit a pull request

## Community

- **Slack:** Join the conversation on the [#maplibre-native](https://osmus.slack.com/archives/C01G4G39862) channel on OSM-US Slack. [Click here for an invite](https://slack.openstreetmap.us/).
- **Website:** [maplibre.org](https://maplibre.org/)

## License

Copyright (c) 2025 MapLibre contributors.

This project is licensed under the BSD 2-Clause License. See the [LICENSE](LICENSE) file for details.

This project integrates multiple components with different licenses:

- MapLibre Native: BSD License
- Slint: GPL-3.0-only OR LicenseRef-Slint-Royalty-free-2.0 OR LicenseRef-Slint-Software-3.0
