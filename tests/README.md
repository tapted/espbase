# espbase Unit Tests

This directory contains standalone unit tests for the `espbase` library. It is designed to mirror the `src` directory structure of `espbase` and compile unit tests locally using GoogleTest.

## Purpose & Architecture

1. **Standalone Dependency Management**: The test suite is completely decoupled from PlatformIO and the main workspace's build configurations. It uses CMake's `FetchContent` to download GoogleTest (v1.17.0) on demand.
2. **Local Emulation**: Unit tests are compiled locally using the emulated headers at `deps/espbase/emulated/esp-idf/include`. This allows rapid development and testing of core C++ library utilities (like `EspResult` and `EspError`) without flashing a physical microcontroller.
3. **Mirrored Layout**: The test folder structure mirrors the `src` layout:
   - `deps/espbase/src/espbase/esp_result.hpp` -> tested by `deps/espbase/tests/espbase/esp_result_test.cpp`.

---

## Getting Started

### CLI Build & Run

Ensure you have `cmake` and a C++ compiler (like GCC, Clang, or MSVC) installed and available in your shell PATH.

1. Navigate to the tests directory:
   ```bash
   cd deps/espbase/tests
   ```
2. Create and navigate to the build directory:
   ```bash
   mkdir build
   cd build
   ```
3. Configure and compile the tests:
   ```bash
   cmake ..
   cmake --build .
   ```
4. Run the tests:
   ```bash
   ctest --output-on-failure
   ```
   Or execute the compiled binary directly:
   ```bash
   ./espbase_tests
   ```

---

## VS Code Integration

To integrate the standalone test suite with the CMake Tools extension in Visual Studio Code alongside your main project directory, configure your workspace settings.

### 1. Update Workspace Settings

Add or modify `"cmake.sourceDirectory"` in your project's `.vscode/settings.json` (making it an array if you have multiple source directories):

```json
{
  "cmake.sourceDirectory": [
    "${workspaceFolder}/sdlmain",
    "${workspaceFolder}/deps/espbase/tests"
  ]
}
```

### 2. Switch Between Active Folders

When both paths are configured:
1. Open the VS Code Command Palette (`Ctrl+Shift+P` or `Cmd+Shift+P`).
2. Run the command: **`CMake: Select Active Folder`**.
3. Select **`deps/espbase/tests`** to configure, compile, and run unit tests.
4. Select **`sdlmain`** (or your main application folder) when you want to switch back to emulator/firmware development.
