# MapLibre Native + Slint Integration

This project demonstrates the integration of MapLibre Native (C++ mapping library) with Slint UI framework for creating interactive map applications.

## Quick Start

For detailed build instructions, see the platform-specific guides:

- **Linux**: [Ubuntu 24.04 Build Guide](docs/build/Linux_Ubuntu_24.md)
- **Windows**: [Windows Build Guide](docs/build/Windows.md) *(coming soon)*
- **macOS**: [macOS Build Guide](docs/build/macOS.md) *(coming soon)*

## Prerequisites

- C++20 compatible compiler
- CMake 3.21 or later
- Rust toolchain (for Slint auto-download)
- OpenGL/GLES development headers
- Network connectivity for downloading dependencies

## Basic Build Process

```bash
# Install dependencies (see platform guides for details)
# Clone and prepare
git clone https://github.com/yuiseki/maplibre-native-slint.git
cd maplibre-native-slint
git submodule update --init --recursive

# Build (Slint will be automatically downloaded if needed)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .

# Run
./maplibre-slint-example
```

**Note**: Slint dependencies are now automatically resolved! CMake will download and build Slint if it's not found system-wide.

## Project Structure

- `src/` - Main source code for the MapLibre-Slint integration
- `examples/` - Example applications
- `platform/` - Platform-specific implementations (RunLoop, FileSource)
- `vendor/` - MapLibre Native submodule
- `CMakeLists.txt` - Build configuration

## Features

- **OpenGL Integration**: Renders MapLibre Native output to OpenGL textures
- **Slint UI Integration**: Displays maps within Slint user interfaces
- **Custom File Source**: HTTP-based tile and resource loading
- **Touch/Mouse Interaction**: Interactive map navigation
- **Cross-platform**: Supports Linux, Windows, and macOS

## Technical Details

### API Compatibility
This project has been updated to work with the latest MapLibre Native APIs:
- Modern RunLoop API using composition instead of inheritance
- Updated ResourceOptions without deprecated methods
- Current HeadlessFrontend construction patterns
- Proper OpenGL context management

### Rendering Pipeline
1. MapLibre Native renders to an OpenGL framebuffer
2. The framebuffer texture is passed to Slint as a `BorrowedOpenGLTexture`
3. Slint displays the texture within its UI components
4. User interactions are forwarded back to MapLibre Native

## Troubleshooting

### Build Issues
- Ensure all dependencies are installed
- Check that Slint is properly installed system-wide
- Verify OpenGL/GLES development headers are available

### Runtime Issues
- For headless environments, set up X11 forwarding or virtual display
- Check that OpenGL drivers are properly installed
- Verify network connectivity for map tile loading

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test the build process
5. Submit a pull request

## License

This project integrates multiple components with different licenses:
- MapLibre Native: BSD License
- Slint: GPL-3.0-only OR LicenseRef-Slint-Royalty-free-2.0 OR LicenseRef-Slint-Software-3.0
- Project code: [Your chosen license]