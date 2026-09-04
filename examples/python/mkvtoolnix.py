import ctypes
from ctypes import (
    c_void_p, c_char_p, c_int, c_uint32, c_int64, c_double, c_size_t,
    Structure, CFUNCTYPE, POINTER, byref
)
import os
import sys
import json
from pathlib import Path
from typing import Callable, Optional, Dict, Any, List

class MtxVersionInfo(Structure):
    _fields_ = [
        ("major", c_uint32),
        ("minor", c_uint32),
        ("patch", c_uint32),
        ("abi_revision", c_uint32),
        ("mkvtoolnix_version", c_char_p),
        ("build_date", c_char_p),
    ]

class MtxProgressInfo(Structure):
    _fields_ = [
        ("percentage", c_int),
        ("current_timestamp_ns", c_int64),
        ("total_duration_ns", c_int64),
        ("bytes_written", c_int64),
    ]

class MtxFileInfo(Structure):
    _fields_ = [
        ("container_format", ctypes.c_char * 64),
        ("duration_ns", c_int64),
        ("track_count", c_uint32),
    ]

class MtxTrackInfo(Structure):
    _fields_ = [
        ("track_id", c_int64),
        ("type", ctypes.c_char * 32),
        ("codec", ctypes.c_char * 64),
        ("language", ctypes.c_char * 32),
        ("language_ietf", ctypes.c_char * 32),
        ("name", ctypes.c_char * 256),
        ("pixel_width", c_uint32),
        ("pixel_height", c_uint32),
        ("display_width", c_uint32),
        ("display_height", c_uint32),
        ("fps", c_double),
        ("audio_channels", c_uint32),
        ("audio_sampling_frequency", c_uint32),
        ("audio_bits_per_sample", c_uint32),
        ("is_default", c_int),
        ("is_forced", c_int),
        ("is_enabled", c_int),
    ]

LOG_CB_T = CFUNCTYPE(None, c_void_p, c_uint32, c_char_p)
PROGRESS_CB_T = CFUNCTYPE(None, c_void_p, POINTER(MtxProgressInfo))

class MkvError(Exception):
    def __init__(self, message: str, code: int = -1):
        super().__init__(message)
        self.code = code

class MkvLibrary:
    _instance: Optional['MkvLibrary'] = None

    def __init__(self, dll_path: Optional[str] = None):
        lib_name = "mkvtoolnix.dll" if sys.platform == "win32" \
                   else ("libmkvtoolnix.dylib" if sys.platform == "darwin" else "libmkvtoolnix.so")

        if dll_path is None:
            # Look in standard build directories relative to repo
            possible_paths = [
                Path(__file__).parent.parent.parent / "out" / "build" / "x64-release" / "Release" / lib_name,
                Path(__file__).parent.parent.parent / "build-linux" / lib_name,
                Path(__file__).parent.parent.parent / "build" / lib_name,
                Path(__file__).parent / lib_name,
                Path(lib_name),
            ]
            for p in possible_paths:
                if p.exists():
                    dll_path = str(p.resolve())
                    break

        if not dll_path or (not os.path.exists(dll_path) and not dll_path.startswith("/")):
            raise FileNotFoundError(f"{lib_name} not found in search paths!")

        # Add Qt and DLL directories to DLL search path on Windows
        if sys.platform == "win32" and hasattr(os, "add_dll_directory"):
            dll_dir = os.path.dirname(os.path.abspath(dll_path))
            os.add_dll_directory(dll_dir)
            qt_bin = "C:\\tmp\\Qt\\6.8.0\\msvc2022_64\\bin"
            if os.path.exists(qt_bin):
                os.add_dll_directory(qt_bin)

        self.dll = ctypes.CDLL(dll_path)
        self._setup_bindings()

    def _setup_bindings(self):
        d = self.dll
        d.mtx_api_version.restype = c_int

        d.mtx_get_version.argtypes = [POINTER(MtxVersionInfo)]
        d.mtx_get_version.restype = c_int

        d.mtx_init.argtypes = [c_uint32]
        d.mtx_init.restype = c_int

        d.mtx_context_create.restype = c_void_p
        d.mtx_context_destroy.argtypes = [c_void_p]
        d.mtx_context_last_error.argtypes = [c_void_p]
        d.mtx_context_last_error.restype = c_char_p
        d.mtx_context_set_log_callback.argtypes = [c_void_p, LOG_CB_T, c_void_p]

        d.mtx_merge_create.argtypes = [c_void_p]
        d.mtx_merge_create.restype = c_void_p
        d.mtx_merge_destroy.argtypes = [c_void_p]

        d.mtx_merge_set_output.argtypes = [c_void_p, c_char_p]
        d.mtx_merge_set_output.restype = c_int

        d.mtx_merge_set_title.argtypes = [c_void_p, c_char_p]
        d.mtx_merge_set_title.restype = c_int

        d.mtx_merge_set_default_language.argtypes = [c_void_p, c_char_p]
        d.mtx_merge_set_default_language.restype = c_int

        d.mtx_merge_set_cluster_max_duration.argtypes = [c_void_p, c_int64]
        d.mtx_merge_set_cluster_max_duration.restype = c_int

        d.mtx_merge_set_track_statistics_tags.argtypes = [c_void_p, c_int]
        d.mtx_merge_set_track_statistics_tags.restype = c_int

        d.mtx_merge_set_deterministic.argtypes = [c_void_p, c_int]
        d.mtx_merge_set_deterministic.restype = c_int

        d.mtx_merge_set_progress_callback.argtypes = [c_void_p, PROGRESS_CB_T, c_void_p]

        d.mtx_merge_add_input.argtypes = [c_void_p, c_char_p, POINTER(c_void_p)]
        d.mtx_merge_add_input.restype = c_int

        d.mtx_merge_prepare.argtypes = [c_void_p]
        d.mtx_merge_prepare.restype = c_int

        d.mtx_merge_execute.argtypes = [c_void_p]
        d.mtx_merge_execute.restype = c_int

        d.mtx_merge_cancel.argtypes = [c_void_p]
        d.mtx_merge_cancel.restype = c_int

        d.mtx_input_get_track_count.argtypes = [c_void_p, c_void_p]
        d.mtx_input_get_track_count.restype = c_size_t

        d.mtx_input_get_track.argtypes = [c_void_p, c_void_p, c_size_t, POINTER(c_void_p)]
        d.mtx_input_get_track.restype = c_int

        d.mtx_input_get_file_info.argtypes = [c_void_p, c_void_p, POINTER(MtxFileInfo)]
        d.mtx_input_get_file_info.restype = c_int

        d.mtx_input_get_track_info.argtypes = [c_void_p, c_void_p, c_size_t, POINTER(MtxTrackInfo)]
        d.mtx_input_get_track_info.restype = c_int

        d.mtx_input_get_json_info.argtypes = [c_void_p, c_void_p]
        d.mtx_input_get_json_info.restype = c_char_p

        d.mtx_track_get_id.argtypes = [c_void_p]
        d.mtx_track_get_id.restype = c_int64

        d.mtx_track_get_type.argtypes = [c_void_p, c_void_p, c_char_p, c_size_t]
        d.mtx_track_get_type.restype = c_int

        d.mtx_track_get_codec.argtypes = [c_void_p, c_void_p, c_char_p, c_size_t]
        d.mtx_track_get_codec.restype = c_int

        d.mtx_track_set_language.argtypes = [c_void_p, c_void_p, c_char_p]
        d.mtx_track_set_language.restype = c_int

        d.mtx_track_set_name.argtypes = [c_void_p, c_void_p, c_char_p]
        d.mtx_track_set_name.restype = c_int

        d.mtx_track_set_default.argtypes = [c_void_p, c_void_p, c_int]
        d.mtx_track_set_default.restype = c_int

        d.mtx_track_set_forced.argtypes = [c_void_p, c_void_p, c_int]
        d.mtx_track_set_forced.restype = c_int

        d.mtx_track_set_enabled.argtypes = [c_void_p, c_void_p, c_int]
        d.mtx_track_set_enabled.restype = c_int

        d.mtx_track_set_hearing_impaired.argtypes = [c_void_p, c_void_p, c_int]
        d.mtx_track_set_hearing_impaired.restype = c_int

        d.mtx_track_set_visual_impaired.argtypes = [c_void_p, c_void_p, c_int]
        d.mtx_track_set_visual_impaired.restype = c_int

        d.mtx_track_set_original.argtypes = [c_void_p, c_void_p, c_int]
        d.mtx_track_set_original.restype = c_int

        d.mtx_track_set_commentary.argtypes = [c_void_p, c_void_p, c_int]
        d.mtx_track_set_commentary.restype = c_int

        d.mtx_track_set_delay.argtypes = [c_void_p, c_void_p, c_int64]
        d.mtx_track_set_delay.restype = c_int

        d.mtx_track_set_sync.argtypes = [c_void_p, c_void_p, c_int64, c_double]
        d.mtx_track_set_sync.restype = c_int

        d.mtx_track_set_display_dimensions.argtypes = [c_void_p, c_void_p, c_uint32, c_uint32]
        d.mtx_track_set_display_dimensions.restype = c_int

        d.mtx_track_set_display_aspect_ratio.argtypes = [c_void_p, c_void_p, c_double]
        d.mtx_track_set_display_aspect_ratio.restype = c_int

        d.mtx_track_set_cropping.argtypes = [c_void_p, c_void_p, c_uint32, c_uint32, c_uint32, c_uint32]
        d.mtx_track_set_cropping.restype = c_int

        d.mtx_track_set_compression.argtypes = [c_void_p, c_void_p, c_int]
        d.mtx_track_set_compression.restype = c_int

        d.mtx_track_set_cues.argtypes = [c_void_p, c_void_p, c_int]
        d.mtx_track_set_cues.restype = c_int

class MkvTrack:
    def __init__(self, merge: 'MkvMerge', handle: c_void_p, track_id: int):
        self._merge = merge
        self._handle = handle
        self._id = track_id

    @property
    def id(self) -> int:
        return self._id

    def get_type(self) -> str:
        buf = ctypes.create_string_buffer(64)
        rc = self._merge.lib.dll.mtx_track_get_type(self._merge.handle, self._handle, buf, len(buf))
        if rc != 0:
            raise MkvError(f"Failed to get track type: {self._merge.context.get_last_error()}", rc)
        return buf.value.decode("utf-8")

    def get_codec(self) -> str:
        buf = ctypes.create_string_buffer(128)
        rc = self._merge.lib.dll.mtx_track_get_codec(self._merge.handle, self._handle, buf, len(buf))
        if rc != 0:
            raise MkvError(f"Failed to get track codec: {self._merge.context.get_last_error()}", rc)
        return buf.value.decode("utf-8")

    def set_language(self, language: str) -> 'MkvTrack':
        rc = self._merge.lib.dll.mtx_track_set_language(self._merge.handle, self._handle, language.encode("utf-8"))
        if rc != 0:
            raise MkvError(f"Failed to set track language: {self._merge.context.get_last_error()}", rc)
        return self

    def set_name(self, name: str) -> 'MkvTrack':
        rc = self._merge.lib.dll.mtx_track_set_name(self._merge.handle, self._handle, name.encode("utf-8"))
        if rc != 0:
            raise MkvError(f"Failed to set track name: {self._merge.context.get_last_error()}", rc)
        return self

    def set_default(self, enabled: bool) -> 'MkvTrack':
        rc = self._merge.lib.dll.mtx_track_set_default(self._merge.handle, self._handle, 1 if enabled else 0)
        if rc != 0:
            raise MkvError(f"Failed to set default flag: {self._merge.context.get_last_error()}", rc)
        return self

    def set_forced(self, enabled: bool) -> 'MkvTrack':
        rc = self._merge.lib.dll.mtx_track_set_forced(self._merge.handle, self._handle, 1 if enabled else 0)
        if rc != 0:
            raise MkvError(f"Failed to set forced flag: {self._merge.context.get_last_error()}", rc)
        return self

    def set_enabled(self, enabled: bool) -> 'MkvTrack':
        rc = self._merge.lib.dll.mtx_track_set_enabled(self._merge.handle, self._handle, 1 if enabled else 0)
        if rc != 0:
            raise MkvError(f"Failed to set enabled flag: {self._merge.context.get_last_error()}", rc)
        return self

    def set_delay(self, delay_ms: int) -> 'MkvTrack':
        rc = self._merge.lib.dll.mtx_track_set_delay(self._merge.handle, self._handle, delay_ms)
        if rc != 0:
            raise MkvError(f"Failed to set delay: {self._merge.context.get_last_error()}", rc)
        return self

    def set_display_dimensions(self, width: int, height: int) -> 'MkvTrack':
        rc = self._merge.lib.dll.mtx_track_set_display_dimensions(self._merge.handle, self._handle, width, height)
        if rc != 0:
            raise MkvError(f"Failed to set display dimensions: {self._merge.context.get_last_error()}", rc)
        return self

    def set_display_aspect_ratio(self, aspect_ratio: float) -> 'MkvTrack':
        rc = self._merge.lib.dll.mtx_track_set_display_aspect_ratio(self._merge.handle, self._handle, aspect_ratio)
        if rc != 0:
            raise MkvError(f"Failed to set aspect ratio: {self._merge.context.get_last_error()}", rc)
        return self

class MkvInput:
    def __init__(self, merge: 'MkvMerge', handle: c_void_p, filename: str):
        self._merge = merge
        self._handle = handle
        self._filename = filename

    @property
    def handle(self) -> c_void_p:
        return self._handle

    @property
    def filename(self) -> str:
        return self._filename

    def get_track_count(self) -> int:
        return self._merge.lib.dll.mtx_input_get_track_count(self._merge.handle, self._handle)

    def get_track(self, index: int) -> MkvTrack:
        track_handle = c_void_p()
        rc = self._merge.lib.dll.mtx_input_get_track(self._merge.handle, self._handle, index, byref(track_handle))
        if rc != 0:
            raise MkvError(f"Failed to get track at index {index}: {self._merge.context.get_last_error()}", rc)
        track_id = self._merge.lib.dll.mtx_track_get_id(track_handle)
        return MkvTrack(self._merge, track_handle, track_id)

    def get_tracks(self) -> List[MkvTrack]:
        count = self.get_track_count()
        return [self.get_track(i) for i in range(count)]

    def identify_json(self) -> Dict[str, Any]:
        json_str = self._merge.lib.dll.mtx_input_get_json_info(self._merge.handle, self._handle)
        if not json_str:
            raise MkvError(f"Failed to identify file {self._filename}: {self._merge.context.get_last_error()}")
        return json.loads(json_str.decode("utf-8"))

    def get_file_info(self) -> Dict[str, Any]:
        info = MtxFileInfo()
        rc = self._merge.lib.dll.mtx_input_get_file_info(self._merge.handle, self._handle, byref(info))
        if rc != 0:
            raise MkvError(f"Failed to get file info: {self._merge.context.get_last_error()}", rc)
        return {
            "container_format": info.container_format.decode("utf-8"),
            "duration_ns": info.duration_ns,
            "track_count": info.track_count,
        }

class MkvContext:
    def __init__(self, lib: Optional[MkvLibrary] = None):
        self.lib = lib or MkvLibrary()
        self.handle = self.lib.dll.mtx_context_create()
        if not self.handle:
            raise MkvError("Failed to create MKVToolNix context")
        self._log_cb_ref = None

    def get_last_error(self) -> str:
        err = self.lib.dll.mtx_context_last_error(self.handle)
        return err.decode("utf-8", errors="replace") if err else ""

    def set_log_callback(self, callback: Callable[[int, str], None]):
        def _c_callback(userdata, level, msg):
            callback(level, msg.decode("utf-8", errors="replace") if msg else "")
        self._log_cb_ref = LOG_CB_T(_c_callback)
        self.lib.dll.mtx_context_set_log_callback(self.handle, self._log_cb_ref, None)

    def create_merge(self) -> 'MkvMerge':
        return MkvMerge(self)

    def destroy(self):
        if self.handle:
            self.lib.dll.mtx_context_destroy(self.handle)
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.destroy()

class MkvMerge:
    def __init__(self, context: MkvContext):
        self.context = context
        self.lib = context.lib
        self.handle = self.lib.dll.mtx_merge_create(context.handle)
        if not self.handle:
            raise MkvError("Failed to create merge engine")
        self._progress_cb_ref = None
        self._inputs: List[MkvInput] = []

    def set_output(self, filename: str) -> 'MkvMerge':
        rc = self.lib.dll.mtx_merge_set_output(self.handle, filename.encode("utf-8"))
        if rc != 0:
            raise MkvError(f"Failed to set output: {self.context.get_last_error()}", rc)
        return self

    def set_title(self, title: str) -> 'MkvMerge':
        rc = self.lib.dll.mtx_merge_set_title(self.handle, title.encode("utf-8"))
        if rc != 0:
            raise MkvError(f"Failed to set title: {self.context.get_last_error()}", rc)
        return self

    def set_default_language(self, language: str) -> 'MkvMerge':
        rc = self.lib.dll.mtx_merge_set_default_language(self.handle, language.encode("utf-8"))
        if rc != 0:
            raise MkvError(f"Failed to set default language: {self.context.get_last_error()}", rc)
        return self

    def set_deterministic(self, enable: bool) -> 'MkvMerge':
        rc = self.lib.dll.mtx_merge_set_deterministic(self.handle, 1 if enable else 0)
        if rc != 0:
            raise MkvError(f"Failed to set deterministic mode: {self.context.get_last_error()}", rc)
        return self

    def add_input(self, filename: str) -> MkvInput:
        inp_handle = c_void_p()
        rc = self.lib.dll.mtx_merge_add_input(self.handle, filename.encode("utf-8"), byref(inp_handle))
        if rc != 0:
            raise MkvError(f"Failed to add input {filename}: {self.context.get_last_error()}", rc)
        inp = MkvInput(self, inp_handle, filename)
        self._inputs.append(inp)
        return inp

    def on_progress(self, callback: Callable[[int, float, float, int], None]) -> 'MkvMerge':
        def _c_cb(userdata, progress_ptr):
            p = progress_ptr.contents
            cur_sec = p.current_timestamp_ns / 1_000_000_000.0
            tot_sec = p.total_duration_ns / 1_000_000_000.0
            callback(p.percentage, cur_sec, tot_sec, p.bytes_written)

        self._progress_cb_ref = PROGRESS_CB_T(_c_cb)
        self.lib.dll.mtx_merge_set_progress_callback(self.handle, self._progress_cb_ref, None)
        return self

    def prepare(self) -> 'MkvMerge':
        rc = self.lib.dll.mtx_merge_prepare(self.handle)
        if rc != 0:
            raise MkvError(f"Failed to prepare merge: {self.context.get_last_error()}", rc)
        return self

    def cancel(self):
        rc = self.lib.dll.mtx_merge_cancel(self.handle)
        if rc != 0:
            raise MkvError(f"Failed to cancel merge: {self.context.get_last_error()}", rc)

    def execute(self):
        rc = self.lib.dll.mtx_merge_execute(self.handle)
        if rc == -6:
            raise MkvError("Merge operation was cancelled by user", -6)
        elif rc != 0:
            raise MkvError(f"Merge execution failed: {self.context.get_last_error()}", rc)

    def destroy(self):
        if self.handle:
            self.lib.dll.mtx_merge_destroy(self.handle)
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.destroy()
