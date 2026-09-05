import { dlopen, FFIType, JSCallback, ptr, CString, toArrayBuffer } from "bun:ffi";
import { resolve, dirname } from "path";
import { existsSync } from "fs";

export interface VersionInfo {
  major: number;
  minor: number;
  patch: number;
  version: string;
  abiRevision: number;
  mkvtoolnixVersion: string;
  buildDate: string;
  platform?: string;
  compiler?: string;
  versionString: string;
}

export interface ProgressInfo {
  percentage: number;
  currentTimestampNs: bigint;
  totalDurationNs: bigint;
  bytesWritten: bigint;
}

export interface FileInfo {
  containerFormat: string;
  durationNs: bigint;
  trackCount: number;
}

export interface TrackInfo {
  trackId: bigint;
  type: string;
  codec: string;
  language: string;
  languageIetf: string;
  name: string;
  pixelWidth: number;
  pixelHeight: number;
  displayWidth: number;
  displayHeight: number;
  fps: number;
  audioChannels: number;
  audioSamplingFrequency: number;
  audioBitsPerSample: number;
  isDefault: boolean;
  isForced: boolean;
  isEnabled: boolean;
}

export class MkvError extends Error {
  public code: number;
  constructor(message: string, code: number = -1) {
    super(message);
    this.name = "MkvError";
    this.code = code;
  }
}

export class MkvLibrary {
  public lib: any;

  constructor(dllPath?: string) {
    const libName = process.platform === "win32" ? "mkvtoolnix.dll"
                  : process.platform === "darwin" ? "libmkvtoolnix.dylib"
                  : "libmkvtoolnix.so";

    if (!dllPath) {
      const candidates = [
        resolve(import.meta.dir, `../../out/build/x64-release/Release/${libName}`),
        resolve(import.meta.dir, `../../build-linux/${libName}`),
        resolve(import.meta.dir, `../../build/${libName}`),
        resolve(import.meta.dir, libName),
        resolve(process.cwd(), `out/build/x64-release/Release/${libName}`),
        resolve(process.cwd(), `build-linux/${libName}`),
        resolve(process.cwd(), `build/${libName}`),
        libName,
      ];
      for (const p of candidates) {
        if (existsSync(p)) {
          dllPath = p;
          break;
        }
      }
    }

    if (!dllPath || (!existsSync(dllPath) && !dllPath.startsWith("/"))) {
      throw new Error(`${libName} nem található a keresési útvonalakon!`);
    }

    this.lib = dlopen(dllPath, {
      mtx_api_version: { args: [], returns: FFIType.i32 },
      mtx_get_version: { args: [FFIType.ptr], returns: FFIType.i32 },
      mtx_get_version_string: { args: [], returns: FFIType.cstring },
      mtx_get_version_json: { args: [], returns: FFIType.cstring },
      mtx_init: { args: [FFIType.u32], returns: FFIType.i32 },

      mtx_context_create: { args: [], returns: FFIType.ptr },
      mtx_context_destroy: { args: [FFIType.ptr], returns: FFIType.void },
      mtx_context_last_error: { args: [FFIType.ptr], returns: FFIType.cstring },
      mtx_context_set_log_callback: { args: [FFIType.ptr, FFIType.function, FFIType.ptr], returns: FFIType.void },

      mtx_merge_create: { args: [FFIType.ptr], returns: FFIType.ptr },
      mtx_merge_destroy: { args: [FFIType.ptr], returns: FFIType.void },
      mtx_merge_set_output: { args: [FFIType.ptr, FFIType.cstring], returns: FFIType.i32 },
      mtx_merge_set_title: { args: [FFIType.ptr, FFIType.cstring], returns: FFIType.i32 },
      mtx_merge_set_default_language: { args: [FFIType.ptr, FFIType.cstring], returns: FFIType.i32 },
      mtx_merge_set_cluster_max_duration: { args: [FFIType.ptr, FFIType.i64], returns: FFIType.i32 },
      mtx_merge_set_track_statistics_tags: { args: [FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
      mtx_merge_set_deterministic: { args: [FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
      mtx_merge_set_progress_callback: { args: [FFIType.ptr, FFIType.function, FFIType.ptr], returns: FFIType.void },

      mtx_merge_add_input: { args: [FFIType.ptr, FFIType.cstring, FFIType.ptr], returns: FFIType.i32 },
      mtx_merge_prepare: { args: [FFIType.ptr], returns: FFIType.i32 },
      mtx_merge_execute: { args: [FFIType.ptr], returns: FFIType.i32 },
      mtx_merge_cancel: { args: [FFIType.ptr], returns: FFIType.i32 },

      mtx_input_get_track_count: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.u64 },
      mtx_input_get_track: { args: [FFIType.ptr, FFIType.ptr, FFIType.u64, FFIType.ptr], returns: FFIType.i32 },
      mtx_input_get_file_info: { args: [FFIType.ptr, FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
      mtx_input_get_track_info: { args: [FFIType.ptr, FFIType.ptr, FFIType.u64, FFIType.ptr], returns: FFIType.i32 },
      mtx_input_get_json_info: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.cstring },

      mtx_track_get_id: { args: [FFIType.ptr], returns: FFIType.i64 },
      mtx_track_get_type: { args: [FFIType.ptr, FFIType.ptr, FFIType.ptr, FFIType.u64], returns: FFIType.i32 },
      mtx_track_get_codec: { args: [FFIType.ptr, FFIType.ptr, FFIType.ptr, FFIType.u64], returns: FFIType.i32 },

      mtx_track_set_language: { args: [FFIType.ptr, FFIType.ptr, FFIType.cstring], returns: FFIType.i32 },
      mtx_track_set_name: { args: [FFIType.ptr, FFIType.ptr, FFIType.cstring], returns: FFIType.i32 },
      mtx_track_set_default: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
      mtx_track_set_forced: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
      mtx_track_set_enabled: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
      mtx_track_set_hearing_impaired: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
      mtx_track_set_visual_impaired: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
      mtx_track_set_original: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
      mtx_track_set_commentary: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },

      mtx_track_set_delay: { args: [FFIType.ptr, FFIType.ptr, FFIType.i64], returns: FFIType.i32 },
      mtx_track_set_sync: { args: [FFIType.ptr, FFIType.ptr, FFIType.i64, FFIType.f64], returns: FFIType.i32 },

      mtx_track_set_display_dimensions: { args: [FFIType.ptr, FFIType.ptr, FFIType.u32, FFIType.u32], returns: FFIType.i32 },
      mtx_track_set_display_aspect_ratio: { args: [FFIType.ptr, FFIType.ptr, FFIType.f64], returns: FFIType.i32 },
      mtx_track_set_cropping: { args: [FFIType.ptr, FFIType.ptr, FFIType.u32, FFIType.u32, FFIType.u32, FFIType.u32], returns: FFIType.i32 },

      mtx_track_set_compression: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
      mtx_track_set_cues: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },

      mtx_input_set_no_attachments: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
      mtx_input_set_no_chapters: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },

      mtx_merge_add_attachment_file: { args: [FFIType.ptr, FFIType.cstring, FFIType.cstring, FFIType.cstring, FFIType.cstring], returns: FFIType.i32 },
      mtx_merge_add_attachment_memory: { args: [FFIType.ptr, FFIType.ptr, FFIType.u64, FFIType.cstring, FFIType.cstring, FFIType.cstring], returns: FFIType.i32 },

      mtx_merge_set_chapters_file: { args: [FFIType.ptr, FFIType.cstring, FFIType.cstring, FFIType.cstring], returns: FFIType.i32 },
      mtx_merge_set_chapters_text: { args: [FFIType.ptr, FFIType.cstring, FFIType.cstring, FFIType.cstring], returns: FFIType.i32 },
      mtx_merge_generate_chapters: { args: [FFIType.ptr, FFIType.i64, FFIType.cstring, FFIType.cstring], returns: FFIType.i32 },
    });
  }

  public get apiVersion(): number {
    return this.lib.symbols.mtx_api_version();
  }

  public get versionString(): string {
    const s = this.lib.symbols.mtx_get_version_string();
    return s ? s.toString() : "";
  }

  public get mkvtoolnixVersion(): string {
    return this.getVersion().mkvtoolnixVersion;
  }

  public get version(): string {
    return this.getVersion().version;
  }

  public getVersion(): VersionInfo {
    const raw = this.lib.symbols.mtx_get_version_json();
    const parsed = JSON.parse(raw ? raw.toString() : "{}");
    return {
      major: parsed.api.major,
      minor: parsed.api.minor,
      patch: parsed.api.patch,
      version: parsed.api.version,
      abiRevision: parsed.abi_revision,
      mkvtoolnixVersion: parsed.mkvtoolnix_version,
      buildDate: parsed.build_date,
      platform: parsed.platform,
      compiler: parsed.compiler,
      versionString: this.versionString,
    };
  }
}

export class MkvTrack {
  constructor(
    private merge: MkvMerge,
    public readonly handle: number,
    public readonly id: bigint
  ) {}

  public getType(): string {
    const buf = new Uint8Array(64);
    const rc = this.merge.library.lib.symbols.mtx_track_get_type(
      this.merge.handle,
      this.handle,
      ptr(buf),
      BigInt(buf.length)
    );
    if (rc !== 0) throw new MkvError(`Nem sikerült lekérni a sáv típusát: ${this.merge.context.getLastError()}`, rc);
    return new CString(ptr(buf)).toString();
  }

  public getCodec(): string {
    const buf = new Uint8Array(128);
    const rc = this.merge.library.lib.symbols.mtx_track_get_codec(
      this.merge.handle,
      this.handle,
      ptr(buf),
      BigInt(buf.length)
    );
    if (rc !== 0) throw new MkvError(`Nem sikerült lekérni a sáv kodekjét: ${this.merge.context.getLastError()}`, rc);
    return new CString(ptr(buf)).toString();
  }

  public setLanguage(language: string): this {
    const rc = this.merge.library.lib.symbols.mtx_track_set_language(
      this.merge.handle,
      this.handle,
      Buffer.from(language + "\0")
    );
    if (rc !== 0) throw new MkvError(`Hiba a sáv nyelvének beállításakor: ${this.merge.context.getLastError()}`, rc);
    return this;
  }

  public setName(name: string): this {
    const rc = this.merge.library.lib.symbols.mtx_track_set_name(
      this.merge.handle,
      this.handle,
      Buffer.from(name + "\0")
    );
    if (rc !== 0) throw new MkvError(`Hiba a sáv nevének beállításakor: ${this.merge.context.getLastError()}`, rc);
    return this;
  }

  public setDefault(enabled: boolean): this {
    const rc = this.merge.library.lib.symbols.mtx_track_set_default(
      this.merge.handle,
      this.handle,
      enabled ? 1 : 0
    );
    if (rc !== 0) throw new MkvError(`Hiba az alapértelmezett flag beállításakor: ${this.merge.context.getLastError()}`, rc);
    return this;
  }

  public setForced(enabled: boolean): this {
    const rc = this.merge.library.lib.symbols.mtx_track_set_forced(
      this.merge.handle,
      this.handle,
      enabled ? 1 : 0
    );
    if (rc !== 0) throw new MkvError(`Hiba a kényszerített flag beállításakor: ${this.merge.context.getLastError()}`, rc);
    return this;
  }

  public setEnabled(enabled: boolean): this {
    const rc = this.merge.library.lib.symbols.mtx_track_set_enabled(
      this.merge.handle,
      this.handle,
      enabled ? 1 : 0
    );
    if (rc !== 0) throw new MkvError(`Hiba az engedélyezett flag beállításakor: ${this.merge.context.getLastError()}`, rc);
    return this;
  }

  public setDelay(delayMs: number | bigint): this {
    const rc = this.merge.library.lib.symbols.mtx_track_set_delay(
      this.merge.handle,
      this.handle,
      BigInt(delayMs)
    );
    if (rc !== 0) throw new MkvError(`Hiba a késleltetés beállításakor: ${this.merge.context.getLastError()}`, rc);
    return this;
  }

  public setDisplayDimensions(width: number, height: number): this {
    const rc = this.merge.library.lib.symbols.mtx_track_set_display_dimensions(
      this.merge.handle,
      this.handle,
      width,
      height
    );
    if (rc !== 0) throw new MkvError(`Hiba a felbontás beállításakor: ${this.merge.context.getLastError()}`, rc);
    return this;
  }

  public setDisplayAspectRatio(aspectRatio: number): this {
    const rc = this.merge.library.lib.symbols.mtx_track_set_display_aspect_ratio(
      this.merge.handle,
      this.handle,
      aspectRatio
    );
    if (rc !== 0) throw new MkvError(`Hiba a képarány beállításakor: ${this.merge.context.getLastError()}`, rc);
    return this;
  }
}

export class MkvInput {
  constructor(
    private merge: MkvMerge,
    public readonly handle: number,
    public readonly filename: string
  ) {}

  public getTrackCount(): number {
    return Number(
      this.merge.library.lib.symbols.mtx_input_get_track_count(this.merge.handle, this.handle)
    );
  }

  public getTrack(index: number): MkvTrack {
    const outPtr = new BigUint64Array(1);
    const rc = this.merge.library.lib.symbols.mtx_input_get_track(
      this.merge.handle,
      this.handle,
      BigInt(index),
      ptr(outPtr)
    );
    if (rc !== 0) throw new MkvError(`Nem sikerült elérni a sávot index=${index}: ${this.merge.context.getLastError()}`, rc);
    const trkHandle = Number(outPtr[0]);
    const trkId = this.merge.library.lib.symbols.mtx_track_get_id(trkHandle);
    return new MkvTrack(this.merge, trkHandle, BigInt(trkId));
  }

  public getTracks(): MkvTrack[] {
    const count = this.getTrackCount();
    const tracks: MkvTrack[] = [];
    for (let i = 0; i < count; i++) {
      tracks.push(this.getTrack(i));
    }
    return tracks;
  }

  public identifyJson(): any {
    const jsonStr = this.merge.library.lib.symbols.mtx_input_get_json_info(
      this.merge.handle,
      this.handle
    );
    if (!jsonStr) {
      throw new MkvError(`JSON azonosítás hiba: ${this.merge.context.getLastError()}`);
    }
    return JSON.parse(jsonStr.toString());
  }

  public setNoAttachments(noAttachments: boolean = true): this {
    const rc = this.merge.library.lib.symbols.mtx_input_set_no_attachments(
      this.merge.handle,
      this.handle,
      noAttachments ? 1 : 0
    );
    if (rc !== 0) throw new MkvError(`Hiba a no-attachments beállításakor: ${this.merge.context.getLastError()}`, rc);
    return this;
  }

  public setNoChapters(noChapters: boolean = true): this {
    const rc = this.merge.library.lib.symbols.mtx_input_set_no_chapters(
      this.merge.handle,
      this.handle,
      noChapters ? 1 : 0
    );
    if (rc !== 0) throw new MkvError(`Hiba a no-chapters beállításakor: ${this.merge.context.getLastError()}`, rc);
    return this;
  }
}

export class MkvContext {
  public handle: number;
  public readonly library: MkvLibrary;

  constructor(library?: MkvLibrary) {
    this.library = library || new MkvLibrary();
    this.handle = this.library.lib.symbols.mtx_context_create();
    if (!this.handle) {
      throw new MkvError("Nem sikerült létrehozni az MKVToolNix kontextust");
    }
  }

  public getLastError(): string {
    const err = this.library.lib.symbols.mtx_context_last_error(this.handle);
    return err ? err.toString() : "";
  }

  public createMerge(): MkvMerge {
    return new MkvMerge(this);
  }

  public destroy(): void {
    if (this.handle) {
      this.library.lib.symbols.mtx_context_destroy(this.handle);
      this.handle = 0;
    }
  }
}

export class MkvMerge {
  public handle: number;
  public readonly library: MkvLibrary;
  private progressCb: any = null;

  constructor(public readonly context: MkvContext) {
    this.library = context.library;
    this.handle = this.library.lib.symbols.mtx_merge_create(context.handle);
    if (!this.handle) {
      throw new MkvError("Nem sikerült létrehozni a merge motort");
    }
  }

  public setOutput(filename: string): this {
    const rc = this.library.lib.symbols.mtx_merge_set_output(
      this.handle,
      Buffer.from(filename + "\0")
    );
    if (rc !== 0) throw new MkvError(`Hiba a kimenet beállításakor: ${this.context.getLastError()}`, rc);
    return this;
  }

  public setTitle(title: string): this {
    const rc = this.library.lib.symbols.mtx_merge_set_title(
      this.handle,
      Buffer.from(title + "\0")
    );
    if (rc !== 0) throw new MkvError(`Hiba a cím beállításakor: ${this.context.getLastError()}`, rc);
    return this;
  }

  public setDefaultLanguage(language: string): this {
    const rc = this.library.lib.symbols.mtx_merge_set_default_language(
      this.handle,
      Buffer.from(language + "\0")
    );
    if (rc !== 0) throw new MkvError(`Hiba az alapértelmezett nyelv beállításakor: ${this.context.getLastError()}`, rc);
    return this;
  }

  public setDeterministic(enabled: boolean): this {
    const rc = this.library.lib.symbols.mtx_merge_set_deterministic(
      this.handle,
      enabled ? 1 : 0
    );
    if (rc !== 0) throw new MkvError(`Hiba a determinisztikus mód beállításakor: ${this.context.getLastError()}`, rc);
    return this;
  }

  public addAttachmentFile(filePath: string, name?: string, mimeType?: string, description?: string): this {
    const rc = this.library.lib.symbols.mtx_merge_add_attachment_file(
      this.handle,
      Buffer.from(filePath + "\0"),
      name ? Buffer.from(name + "\0") : null,
      mimeType ? Buffer.from(mimeType + "\0") : null,
      description ? Buffer.from(description + "\0") : null
    );
    if (rc !== 0) throw new MkvError(`Hiba a csatolmány hozzáadásakor (${filePath}): ${this.context.getLastError()}`, rc);
    return this;
  }

  public addAttachmentMemory(data: Uint8Array, name: string, mimeType?: string, description?: string): this {
    const rc = this.library.lib.symbols.mtx_merge_add_attachment_memory(
      this.handle,
      ptr(data),
      BigInt(data.byteLength),
      Buffer.from(name + "\0"),
      mimeType ? Buffer.from(mimeType + "\0") : null,
      description ? Buffer.from(description + "\0") : null
    );
    if (rc !== 0) throw new MkvError(`Hiba a memóriacsatolmány hozzáadásakor (${name}): ${this.context.getLastError()}`, rc);
    return this;
  }

  public setChaptersFile(filePath: string, language?: string, charset?: string): this {
    const rc = this.library.lib.symbols.mtx_merge_set_chapters_file(
      this.handle,
      Buffer.from(filePath + "\0"),
      language ? Buffer.from(language + "\0") : null,
      charset ? Buffer.from(charset + "\0") : null
    );
    if (rc !== 0) throw new MkvError(`Hiba a fejezetek beállításakor (${filePath}): ${this.context.getLastError()}`, rc);
    return this;
  }

  public setChaptersText(chapterText: string, language?: string, charset?: string): this {
    const rc = this.library.lib.symbols.mtx_merge_set_chapters_text(
      this.handle,
      Buffer.from(chapterText + "\0"),
      language ? Buffer.from(language + "\0") : null,
      charset ? Buffer.from(charset + "\0") : null
    );
    if (rc !== 0) throw new MkvError(`Hiba a fejezetek beállításakor szövegből: ${this.context.getLastError()}`, rc);
    return this;
  }

  public generateChapters(intervalMs: number, language?: string, nameTemplate?: string): this {
    const rc = this.library.lib.symbols.mtx_merge_generate_chapters(
      this.handle,
      BigInt(intervalMs),
      language ? Buffer.from(language + "\0") : null,
      nameTemplate ? Buffer.from(nameTemplate + "\0") : null
    );
    if (rc !== 0) throw new MkvError(`Hiba az automatikus fejezetgenerálás beállításakor: ${this.context.getLastError()}`, rc);
    return this;
  }

  public addInput(filename: string): MkvInput {
    const outPtr = new BigUint64Array(1);
    const rc = this.library.lib.symbols.mtx_merge_add_input(
      this.handle,
      Buffer.from(filename + "\0"),
      ptr(outPtr)
    );
    if (rc !== 0) throw new MkvError(`Hiba a bemenet megnyitásakor (${filename}): ${this.context.getLastError()}`, rc);
    return new MkvInput(this, Number(outPtr[0]), filename);
  }

  public onProgress(callback: (percentage: number, curSec: number, totSec: number, bytesWritten: number) => void): this {
    this.progressCb = new JSCallback(
      (_userdata: number, progressInfoPtr: number) => {
        const view = new DataView(toArrayBuffer(progressInfoPtr, 0, 32));
        const percentage = view.getInt32(0, true);
        const curTs = view.getBigInt64(8, true);
        const totDur = view.getBigInt64(16, true);
        const bytesWr = view.getBigInt64(24, true);

        const curSec = Number(curTs) / 1_000_000_000;
        const totSec = Number(totDur) / 1_000_000_000;
        callback(percentage, curSec, totSec, Number(bytesWr));
      },
      {
        args: [FFIType.ptr, FFIType.ptr],
        returns: FFIType.void,
      }
    );

    this.library.lib.symbols.mtx_merge_set_progress_callback(
      this.handle,
      this.progressCb.ptr,
      null
    );
    return this;
  }

  public prepare(): this {
    const rc = this.library.lib.symbols.mtx_merge_prepare(this.handle);
    if (rc !== 0) throw new MkvError(`Hiba a prepare fázisban: ${this.context.getLastError()}`, rc);
    return this;
  }

  public cancel(): void {
    const rc = this.library.lib.symbols.mtx_merge_cancel(this.handle);
    if (rc !== 0) throw new MkvError(`Hiba a megszakítás kérésekor: ${this.context.getLastError()}`, rc);
  }

  public execute(): Promise<void> {
    return new Promise((resolve, reject) => {
      const rc = this.library.lib.symbols.mtx_merge_execute(this.handle);
      if (rc === 0) {
        resolve();
      } else if (rc === -6) {
        reject(new MkvError("A műveletet a felhasználó megszakította (Cancelled)", -6));
      } else {
        reject(new MkvError(`Hiba a muxing során: ${this.context.getLastError()}`, rc));
      }
    });
  }

  public destroy(): void {
    if (this.progressCb) {
      this.progressCb.close();
      this.progressCb = null;
    }
    if (this.handle) {
      this.library.lib.symbols.mtx_merge_destroy(this.handle);
      this.handle = 0;
    }
  }
}
