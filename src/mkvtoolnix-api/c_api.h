#pragma once

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(MTX_API_BUILD)
#    define MTX_API_EXPORT __declspec(dllexport)
#  else
#    define MTX_API_EXPORT __declspec(dllimport)
#  endif
#  define MTX_API_CALL __cdecl
#else
#  define MTX_API_EXPORT __attribute__((visibility("default")))
#  define MTX_API_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* 1. OPAQUE HANDLES (100% ABI STABLE ACROSS VERSIONS)                       */
/* ========================================================================= */

typedef struct mtx_context mtx_context_t;
typedef struct mtx_merge   mtx_merge_t;
typedef struct mtx_input   mtx_input_t;
typedef struct mtx_track   mtx_track_t;

/* Backwards compatibility aliases */
typedef mtx_context_t mtx_context;
typedef mtx_merge_t   mtx_merge;
typedef mtx_input_t   mtx_input;
typedef mtx_track_t   mtx_track;

/* ========================================================================= */
/* 2. CONSTANTS & STATUS CODES                                               */
/* ========================================================================= */

#define MTX_API_VERSION_MAJOR 1
#define MTX_API_VERSION_MINOR 0
#define MTX_API_VERSION_PATCH 0

enum {
  MTX_OK                    = 0,
  MTX_ERROR_INVALID_ARG     = -1,
  MTX_ERROR_INVALID_STATE   = -2,
  MTX_ERROR_EXCEPTION       = -3,
  MTX_ERROR_FILE_NOT_FOUND  = -4,
  MTX_ERROR_UNSUPPORTED     = -5,
  MTX_ERROR_CANCELLED       = -6,
};

enum {
  MTX_LOG_INFO    = 0,
  MTX_LOG_WARNING = 1,
  MTX_LOG_ERROR   = 2,
};

enum {
  MTX_COMPRESSION_DEFAULT = 0,
  MTX_COMPRESSION_NONE    = 1,
  MTX_COMPRESSION_ZLIB    = 2,
};

enum {
  MTX_CUES_DEFAULT = 0,
  MTX_CUES_ALL     = 1,
  MTX_CUES_IFRAMES = 2,
  MTX_CUES_NONE    = 3,
};

/* ========================================================================= */
/* 3. STRUCTS FOR VERSION, PROGRESS & METADATA                               */
/* ========================================================================= */

typedef struct {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
  uint32_t abi_revision;
  const char *mkvtoolnix_version;
  const char *build_date;
} mtx_version_info_t;

typedef struct {
  int percentage;               /* 0 - 100 */
  int64_t current_timestamp_ns; /* Current stream position in nanoseconds */
  int64_t total_duration_ns;    /* Total estimated duration in nanoseconds */
  int64_t bytes_written;        /* Bytes written to destination so far */
} mtx_progress_info_t;

typedef struct {
  char container_format[64];
  int64_t duration_ns;
  uint32_t track_count;
} mtx_file_info_t;
typedef mtx_file_info_t mtx_file_info;

typedef struct {
  int64_t track_id;
  char type[32];
  char codec[64];
  char language[32];
  char language_ietf[32];
  char name[256];

  /* Video properties */
  uint32_t pixel_width;
  uint32_t pixel_height;
  uint32_t display_width;
  uint32_t display_height;
  double fps;

  /* Audio properties */
  uint32_t audio_channels;
  uint32_t audio_sampling_frequency;
  uint32_t audio_bits_per_sample;

  /* Flags */
  int is_default;
  int is_forced;
  int is_enabled;
} mtx_track_info_t;
typedef mtx_track_info_t mtx_track_info;

/* ========================================================================= */
/* 4. CALLBACK SIGNATURES                                                    */
/* ========================================================================= */

typedef void (MTX_API_CALL *mtx_progress_callback)(void *userdata, const mtx_progress_info_t *progress);
typedef void (MTX_API_CALL *mtx_log_callback)(void *userdata, unsigned int level, const char *message);

/* ========================================================================= */
/* 5. VERSIONING & INITIALIZATION                                            */
/* ========================================================================= */

MTX_API_EXPORT int MTX_API_CALL mtx_api_version(void);
MTX_API_EXPORT int MTX_API_CALL mtx_get_version(mtx_version_info_t *info);
MTX_API_EXPORT int MTX_API_CALL mtx_init(uint32_t expected_major_version);

/* ========================================================================= */
/* 6. CONTEXT MANAGEMENT & ERROR HANDLING                                    */
/* ========================================================================= */

MTX_API_EXPORT mtx_context_t *MTX_API_CALL mtx_context_create(void);
MTX_API_EXPORT void MTX_API_CALL mtx_context_destroy(mtx_context_t *context);
MTX_API_EXPORT const char *MTX_API_CALL mtx_context_last_error(mtx_context_t const *context);
MTX_API_EXPORT void MTX_API_CALL mtx_context_set_log_callback(mtx_context_t *context, mtx_log_callback callback, void *userdata);

/* ========================================================================= */
/* 7. MERGE ENGINE CONFIGURATION & LIFECYCLE                                 */
/* ========================================================================= */

MTX_API_EXPORT mtx_merge_t *MTX_API_CALL mtx_merge_create(mtx_context_t *context);
MTX_API_EXPORT void MTX_API_CALL mtx_merge_destroy(mtx_merge_t *merge);

MTX_API_EXPORT int MTX_API_CALL mtx_merge_set_output(mtx_merge_t *merge, const char *filename);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_set_title(mtx_merge_t *merge, const char *title);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_set_default_language(mtx_merge_t *merge, const char *language);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_set_cluster_max_duration(mtx_merge_t *merge, int64_t max_ms);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_set_track_statistics_tags(mtx_merge_t *merge, int enable);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_set_deterministic(mtx_merge_t *merge, int enable);
MTX_API_EXPORT void MTX_API_CALL mtx_merge_set_progress_callback(mtx_merge_t *merge, mtx_progress_callback callback, void *userdata);

MTX_API_EXPORT int MTX_API_CALL mtx_merge_add_input(mtx_merge_t *merge, const char *filename, mtx_input_t **result);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_prepare(mtx_merge_t *merge);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_execute(mtx_merge_t *merge);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_cancel(mtx_merge_t *merge);

/* ========================================================================= */
/* 8. INPUT & TRACK INSPECTION (IDENTIFY / JSON)                             */
/* ========================================================================= */

MTX_API_EXPORT size_t MTX_API_CALL mtx_input_get_track_count(mtx_merge_t *merge, mtx_input_t *input);
MTX_API_EXPORT int MTX_API_CALL mtx_input_get_track(mtx_merge_t *merge, mtx_input_t *input, size_t index, mtx_track_t **result);
MTX_API_EXPORT int MTX_API_CALL mtx_input_get_file_info(mtx_merge_t *merge, mtx_input_t *input, mtx_file_info_t *result);
MTX_API_EXPORT int MTX_API_CALL mtx_input_get_track_info(mtx_merge_t *merge, mtx_input_t *input, size_t index, mtx_track_info_t *result);
MTX_API_EXPORT const char *MTX_API_CALL mtx_input_get_json_info(mtx_merge_t *merge, mtx_input_t *input);

/* ========================================================================= */
/* 9. TYPE-SAFE TRACK GETTERS & SETTERS                                      */
/* ========================================================================= */

MTX_API_EXPORT int64_t MTX_API_CALL mtx_track_get_id(mtx_track_t const *track);
MTX_API_EXPORT int MTX_API_CALL mtx_track_get_type(mtx_merge_t *merge, mtx_track_t const *track, char *buf, size_t buf_size);
MTX_API_EXPORT int MTX_API_CALL mtx_track_get_codec(mtx_merge_t *merge, mtx_track_t const *track, char *buf, size_t buf_size);

MTX_API_EXPORT int MTX_API_CALL mtx_track_set_language(mtx_merge_t *merge, mtx_track_t *track, const char *language);
MTX_API_EXPORT int MTX_API_CALL mtx_track_set_name(mtx_merge_t *merge, mtx_track_t *track, const char *name);
MTX_API_EXPORT int MTX_API_CALL mtx_track_set_default(mtx_merge_t *merge, mtx_track_t *track, int enabled);
MTX_API_EXPORT int MTX_API_CALL mtx_track_set_forced(mtx_merge_t *merge, mtx_track_t *track, int enabled);
MTX_API_EXPORT int MTX_API_CALL mtx_track_set_enabled(mtx_merge_t *merge, mtx_track_t *track, int enabled);
MTX_API_EXPORT int MTX_API_CALL mtx_track_set_hearing_impaired(mtx_merge_t *merge, mtx_track_t *track, int enabled);
MTX_API_EXPORT int MTX_API_CALL mtx_track_set_visual_impaired(mtx_merge_t *merge, mtx_track_t *track, int enabled);
MTX_API_EXPORT int MTX_API_CALL mtx_track_set_original(mtx_merge_t *merge, mtx_track_t *track, int enabled);
MTX_API_EXPORT int MTX_API_CALL mtx_track_set_commentary(mtx_merge_t *merge, mtx_track_t *track, int enabled);

MTX_API_EXPORT int MTX_API_CALL mtx_track_set_delay(mtx_merge_t *merge, mtx_track_t *track, int64_t delay_ms);
MTX_API_EXPORT int MTX_API_CALL mtx_track_set_sync(mtx_merge_t *merge, mtx_track_t *track, int64_t delay_ms, double stretch_factor);

MTX_API_EXPORT int MTX_API_CALL mtx_track_set_display_dimensions(mtx_merge_t *merge, mtx_track_t *track, uint32_t width, uint32_t height);
MTX_API_EXPORT int MTX_API_CALL mtx_track_set_display_aspect_ratio(mtx_merge_t *merge, mtx_track_t *track, double aspect_ratio);
MTX_API_EXPORT int MTX_API_CALL mtx_track_set_cropping(mtx_merge_t *merge, mtx_track_t *track, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom);

MTX_API_EXPORT int MTX_API_CALL mtx_track_set_compression(mtx_merge_t *merge, mtx_track_t *track, int compression_mode);
MTX_API_EXPORT int MTX_API_CALL mtx_track_set_cues(mtx_merge_t *merge, mtx_track_t *track, int cue_mode);

/* ========================================================================= */
/* 10. ATTACHMENT SUPPORT                                                    */
/* ========================================================================= */

MTX_API_EXPORT int MTX_API_CALL mtx_input_set_no_attachments(mtx_merge_t *merge, mtx_input_t *input, int no_attachments);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_add_attachment_file(mtx_merge_t *merge, const char *file_path, const char *name, const char *mime_type, const char *description);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_add_attachment_memory(mtx_merge_t *merge, const void *data, size_t size, const char *name, const char *mime_type, const char *description);

/* ========================================================================= */
/* 11. CHAPTER SUPPORT                                                       */
/* ========================================================================= */

MTX_API_EXPORT int MTX_API_CALL mtx_input_set_no_chapters(mtx_merge_t *merge, mtx_input_t *input, int no_chapters);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_set_chapters_file(mtx_merge_t *merge, const char *file_path, const char *language, const char *charset);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_set_chapters_text(mtx_merge_t *merge, const char *chapter_text, const char *language, const char *charset);
MTX_API_EXPORT int MTX_API_CALL mtx_merge_generate_chapters(mtx_merge_t *merge, int64_t interval_ms, const char *language, const char *name_template);

#ifdef __cplusplus
}
#endif
