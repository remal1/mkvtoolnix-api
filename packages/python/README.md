# mkvtoolnix (Python FFI Bindings)

Production-grade, zero-dependency Python bindings for the headless MKVToolNix C ABI (`mkvtoolnix.dll`).

## Features
- **Pure Python standard library** (`ctypes`), zero external pip dependencies.
- **Media Inspection**: Full JSON identification (`mkvmerge -J` parity) and structured metadata.
- **Remuxing & Merging**: Type-safe track property control (BCP 47 language, titles, delay, cropping, display dimensions, flags).
- **Real-Time Progress**: Callbacks reporting percentage, timestamps (ns), and byte counts.
- **Cancellation**: Thread-safe abort (`cancel()`) with automatic partial file cleanup.
- **Exception Barrier**: Zero C++ exceptions leaking past DLL boundaries.

## Usage Example

```python
from mkvtoolnix import MkvContext

with MkvContext() as ctx:
    merge = ctx.create_merge()
    merge.set_output("output.mkv")
    merge.set_title("My Movie")

    # Add input and inspect tracks
    video_input = merge.add_input("input.mp4")
    merge.prepare()

    for track in video_input.get_tracks():
        if track.get_type() == "audio":
            track.set_language("hun")
            track.set_name("Magyar Szinkron")
            track.set_default(True)

    merge.on_progress(lambda pct, cur, tot, bytes_w: print(f"Progress: {pct}%"))
    merge.execute()
```
