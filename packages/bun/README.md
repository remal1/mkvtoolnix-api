# @mkvtoolnix/bun (Bun TypeScript FFI Bindings)

Production-grade TypeScript FFI bindings for the headless MKVToolNix C ABI (`mkvtoolnix.dll`) using Bun's ultra-fast native `bun:ffi` engine.

## Features
- **Native `bun:ffi` Integration**: High-performance C ABI bridging with zero compilation overhead.
- **Media Inspection**: Full JSON identification (`mkvmerge -J` parity) and structured track metadata.
- **Remuxing & Merging**: Type-safe track property control (BCP 47 language, titles, delay, cropping, display dimensions, flags).
- **Real-Time Progress**: `JSCallback` reporting progress percentage, timestamp positions (s), and bytes written.
- **Cancellation**: Asynchronous abort (`cancel()`) with automatic deletion of partial output files.
- **Exception Barrier**: Zero C++ exceptions leaking past DLL boundaries.

## Usage Example

```typescript
import { MkvContext } from "@mkvtoolnix/bun";

const ctx = new MkvContext();
const merge = ctx.createMerge();

try {
  merge.setOutput("output.mkv");
  merge.setTitle("My Movie");

  const input = merge.addInput("input.mp4");
  merge.prepare();

  for (const track of input.getTracks()) {
    if (track.getType() === "audio") {
      track.setLanguage("hun");
      track.setName("Magyar Szinkron");
      track.setDefault(true);
    }
  }

  merge.onProgress((pct, curSec, totSec, bytesWritten) => {
    console.log(`Progress: ${pct}% (${curSec.toFixed(1)}s / ${totSec.toFixed(1)}s)`);
  });

  await merge.execute();
  console.log("Remuxing complete!");
} finally {
  merge.destroy();
  ctx.destroy();
}
```
