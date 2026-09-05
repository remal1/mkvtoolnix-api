# MKVToolNix API

A lightweight, high-performance C/C++ shared library (`.dll`) wrapper around [MKVToolNix](https://codeberg.org/mbunkus/mkvtoolnix) dedicated exclusively to **headless media track remuxing, metadata tagging, container repacking, media identification, and track property inspection**.

Designed for seamless integration into multi-language applications (**Bun JS**, **Node.js**, **Python**, **Go**, **Rust**, **C#**, **C/C++**, etc.) with **type-safe track property control**, **in-process media probing (`mkvmerge -J` JSON & C structs)**, **real-time progress reporting**, and **thread-safe cancellation**.

---

## Key Features

- **Pure C ABI (`c_api.h`)**: Effortlessly callable from any runtime via FFI (`bun:ffi`, Node.js `koffi`/`ffi-napi`, Python `ctypes`/`cffi`, Go `cgo`/`purego`, Rust `bindgen`, C# `P/Invoke`).
- **100% Opaque Handles**: Handles (`mtx_context_t*`, `mtx_merge_t*`, `mtx_input_t*`, `mtx_track_t*`) ensure complete ABI stability across upstream MKVToolNix versions.
- **Zero C++ Exception Leakage**: Hard exception barrier catches all C++ exceptions (`mtx::exception`, `std::exception`) and surfaces clean error codes (`MTX_ERROR_*`) with error descriptions via `mtx_context_last_error()`.
- **In-Process Media Identification (`mtx_input_get_json_info` & `mtx_input_get_file_info`)**: Instantly inspect container formats, track counts, codecs, resolutions, framerates, audio channels, sampling rates, BCP 47 language tags, and metadata without spawning external CLI processes.
- **Type-Safe Track Configuration**: Dedicated C functions for BCP 47 languages, track titles, default/forced/enabled/impaired flags, audio delays, sync stretching, display dimensions, aspect ratios, cropping, and cues.
- **Thread-Safe Cancellation (`mtx_merge_cancel`)**: Safely abort running merge operations from any thread or timer, with automatic deletion of incomplete destination files.
- **Real-Time Byte-Accurate Progress Reporting**: Periodic callback reporting completion percentage (0–100%), stream timestamps in nanoseconds, total duration, and bytes written.
- **100% CLI Parity**: Output files and metadata verified bit-for-bit against official `mkvmerge` CLI output.

---

## Project Structure & SDKs

```text
mkvtoolnix-api/
├── CMakeLists.txt                    # Root CMake build configuration
├── CMakePresets.json                 # Standardized MSVC presets (x64-release)
├── patches/
│   └── mkvtoolnix.patch              # Unified patch for MKVToolNix core (auto-applied)
├── src/
│   └── mkvtoolnix-api/
│       ├── c_api.h                  # Public C ABI header (opaque handles)
│       └── c_api.cpp                # C ABI implementation & engine bridge
├── packages/
│   ├── bun/                          # Standalone Bun TypeScript package (@mkvtoolnix/bun)
│   │   ├── package.json
│   │   └── src/index.ts              #   Native bun:ffi OOP wrapper
│   └── python/                       # Standalone Python package (pyproject.toml)
│       └── mkvtoolnix/               #   Zero-dependency ctypes wrapper
├── examples/
│   ├── bun/                          # Bun JS / TypeScript Examples (bun:ffi)
│   │   ├── mkvtoolnix.ts             # Direct wrapper module
│   │   └── demo.ts                   # Integration & CLI parity test suite
│   ├── python/                       # Python Examples (ctypes)
│   │   ├── mkvtoolnix.py             # Direct wrapper module
│   │   └── demo.py                   # Integration & CLI parity test suite
│   ├── minimal.c                     # Minimal C native consumer
│   └── identify.c                    # Media inspection in native C
└── third_party/
    └── mkvtoolnix/                   # Git submodule (official MKVToolNix upstream)
```

---

## Quick Start & Build

### 1. Clone with Submodules

```bash
git clone --recursive https://github.com/remal1/mkvtoolnix-api.git
cd mkvtoolnix-api
```

*(If already cloned without `--recursive`, run `git submodule update --init --recursive`)*

### 2. Windows Build

#### Prerequisites
- **Visual Studio 2022 / 2026** (MSVC v19.40+, C++20 standard)
- **CMake 3.20+**
- **Qt 6 Core** (headless, e.g. Qt 6.8.0 Core)
- **vcpkg packages**: `boost-filesystem`, `boost-system`, `boost-locale`, `flac`, `libogg`, `libvorbis`, `libiconv`, `gettext-libintl`, `mpir`, `zlib`.

#### Build Command
```powershell
# Configure (automatically checks and applies patches/mkvtoolnix.patch)
cmake --preset x64-release

# Build mkvtoolnix.dll
cmake --build --preset x64-release --config Release
```
The compiled library (`mkvtoolnix.dll`) is placed in `out/build/x64-release/Release/`.

### 3. Linux Build (Ubuntu / Debian / WSL2)

#### Prerequisites
```bash
sudo apt update && sudo apt install -y \
    build-essential cmake pkg-config \
    qt6-base-dev \
    libboost-filesystem-dev libboost-system-dev libboost-locale-dev \
    libflac-dev libogg-dev libvorbis-dev \
    libgmp-dev zlib1g-dev
```

#### Build Command
```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build libmkvtoolnix.so
cmake --build build -j$(nproc)
```
The compiled library (`libmkvtoolnix.so`) is placed in `build/`.

### 4. macOS Build (Homebrew)

#### Prerequisites
```bash
brew install cmake pkg-config qt@6 boost flac libogg libvorbis gmp zlib
```

#### Build Command
```bash
# Configure with Qt6 path
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"

# Build libmkvtoolnix.dylib
cmake --build build -j$(sysctl -n hw.ncpu)
```
The compiled library (`libmkvtoolnix.dylib`) is placed in `build/`.

---

## Version Information Query

Both Bun and Python SDKs provide clean, high-level methods to inspect versions and runtime environment:

### Bun JS / TypeScript
```typescript
import { MkvLibrary } from "./packages/bun/src/index";

const lib = new MkvLibrary();
console.log(lib.apiVersion);        // 1
console.log(lib.version);           // "1.0.0"
console.log(lib.mkvtoolnixVersion); // "101.0"
console.log(lib.versionString);     // "mkvtoolnix-api v1.0.0 (ABI rev 1, MKVToolNix 101.0, built ...)"
console.log(lib.getVersion());      // { major: 1, minor: 0, patch: 0, abiRevision: 1, mkvtoolnixVersion: "101.0", ... }
```

### Python
```python
from mkvtoolnix import MkvLibrary

lib = MkvLibrary()
print(lib.api_version)        # 1
print(lib.version)            # "1.0.0"
print(lib.mkvtoolnix_version) # "101.0"
print(lib.version_string)     # "mkvtoolnix-api v1.0.0 (ABI rev 1, MKVToolNix 101.0, built ...)"
print(lib.get_version())      # { 'major': 1, 'minor': 0, 'patch': 0, 'abi_revision': 1, 'mkvtoolnix_version': '101.0', ... }
```

---

## High-Level SDK Usage

### Bun JS / TypeScript

```typescript
import { MkvContext } from "./packages/bun/src/index";

const ctx = new MkvContext();
const merge = ctx.createMerge();

try {
  // 1. Configure output & container
  merge.setOutput("output.mkv");
  merge.setTitle("My Remuxed Movie");

  // 2. Add input and inspect tracks
  const input = merge.addInput("input.webm");
  merge.prepare();

  // 3. Configure tracks with type-safe methods
  for (const track of input.getTracks()) {
    if (track.getType() === "audio") {
      track.setLanguage("hun");
      track.setName("Magyar Opus 5.1");
      track.setDefault(true);
      track.setDelay(250); // +250ms
    }
  }

  // 4. Add attachments & chapters
  merge.addAttachmentFile("cover.jpg", "cover.jpg", "image/jpeg", "Movie Poster");
  merge.setChaptersText("CHAPTER01=00:00:00.000\nCHAPTER01NAME=Intro\nCHAPTER02=00:00:10.000\nCHAPTER02NAME=Main");

  // 5. Real-time progress callback
  merge.onProgress((pct, curSec, totSec, bytesWritten) => {
    console.log(`Progress: ${pct}% (${curSec.toFixed(1)}s / ${totSec.toFixed(1)}s) | Written: ${(bytesWritten / 1024).toFixed(0)} KB`);
  });

  // 5. Execute remuxing
  await merge.execute();
  console.log("Remuxing completed successfully!");
} finally {
  merge.destroy();
  ctx.destroy();
}
```

### Python

```python
from mkvtoolnix import MkvContext

with MkvContext() as ctx:
    merge = ctx.create_merge()
    merge.set_output("output.mkv")
    merge.set_title("My Remuxed Movie")

    # Add input and inspect tracks
    video_input = merge.add_input("input.webm")
    merge.prepare()

    for track in video_input.get_tracks():
        if track.get_type() == "audio":
            track.set_language("hun")
            track.set_name("Magyar Opus 5.1")
            track.set_default(True)
            track.set_delay(250)  # +250ms

    # Add attachments & chapters
    merge.add_attachment_file("cover.jpg", name="cover.jpg", mime_type="image/jpeg", description="Movie Poster")
    merge.set_chapters_text("CHAPTER01=00:00:00.000\nCHAPTER01NAME=Intro\nCHAPTER02=00:00:10.000\nCHAPTER02NAME=Main")

    # Real-time progress
    merge.on_progress(lambda pct, cur, tot, b: print(f"{pct}% ({cur:.1f}s / {tot:.1f}s)"))

    # Execute
    merge.execute()
    print("Remuxing completed successfully!")
```

---

## Running Examples & Parity Tests

### Bun Examples

```bash
# Run Bun FFI integration & CLI parity test suite
bun run examples/bun/demo.ts
```

### Python Examples

```bash
# Run Python ctypes integration & CLI parity test suite
python examples/python/demo.py
```

Both test suites perform:
1. Version handshake and capability inspection.
2. Media identify / JSON metadata extraction.
3. Track configuration (BCP 47 language, titles, flags, dimensions, audio delay).
4. Attachments (file & memory buffer) and chapters (OGG simple / XML) muxing.
5. Verification of attachments and chapters via JSON identification.
6. Input attachment and chapter suppression (`--no-attachments`, `--no-chapters`).
7. Real-time progress callback execution.
8. Asynchronous cancellation with automatic partial file cleanup.
9. 1:1 parity comparison against official `mkvmerge` CLI output.

---

## License

GPL v2.0 License (derived from MKVToolNix). See `COPYING` for details.
