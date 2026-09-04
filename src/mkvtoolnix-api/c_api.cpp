#include "common/common_pch.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/bcp47.h"
#include "common/id_info.h"
#include "common/json.h"
#include "common/output.h"
#include "common/version.h"
#include "merge/cluster_helper.h"
#include "merge/filelist.h"
#include "merge/generic_reader.h"
#include "merge/id_result.h"
#include "merge/output_control.h"
#include "merge/reader_detection_and_creation.h"
#include "merge/track_info.h"

#include "mkvtoolnix-api/c_api.h"

struct mtx_input {
  uint32_t index;
};

struct mtx_track {
  uint32_t input_index;
  int64_t track_id;
};

struct mtx_context {
  std::string last_error;
  std::string last_json_info;
  mtx_log_callback log_callback{};
  void *log_userdata{};
};

struct mtx_merge {
  mtx_context_t *context{};
  bool headers_read{};
  bool executed{};
  mtx_progress_callback progress_callback{};
  void *progress_userdata{};
  std::vector<std::unique_ptr<mtx_input_t>> inputs;
  std::vector<std::unique_ptr<mtx_track_t>> tracks;
};

namespace {

class api_error_x: public std::runtime_error {
public:
  explicit api_error_x(std::string const &message)
    : std::runtime_error{message}
  {
  }
};

std::once_flag s_common_init;
std::mutex s_engine_mutex;
mtx_context *s_active_context{};

void init_common() {
  std::call_once(s_common_init, [] {
    mtx_common_init("mkvtoolnix", nullptr);
  });
}

void reset_merge_state() {
  cleanup();

  g_outfile.clear();
  g_timestamp_scale = TIMESTAMP_SCALE;
  g_timestamp_scale_mode = TIMESTAMP_SCALE_MODE_NORMAL;
  g_write_meta_seek_for_clusters = false;
  g_chapter_file_name.clear();
  g_chapter_language.clear();
  g_chapter_charset.clear();
  g_segmentinfo_file_name.clear();
  g_segment_title.clear();
  g_segment_title_set = false;
  g_segment_filename.clear();
  g_previous_segment_filename.clear();
  g_next_segment_filename.clear();
  g_default_language = mtx::bcp47::language_c::parse("und");
  g_video_fps = 0.0;
  g_video_packetizer = nullptr;
  g_write_cues = true;
  g_cue_writing_requested = false;
  g_write_date = true;
  g_stop_after_video_ends = false;
  g_no_lacing = false;
  g_no_linking = true;
  g_use_durations = false;
  g_no_track_statistics_tags = false;
  g_identifying = false;
  g_file_num = 1;
  g_file_sizes = 0;
  g_max_ns_per_cluster = 5000000000ll;
  g_max_blocks_per_cluster = 65535;
  g_split_max_num_files = 65535;
  g_splitting_by_chapter_numbers.clear();
  g_splitting_by_all_chapters = false;
  g_append_mode = APPEND_MODE_FILE_BASED;
  g_deterministic = false;
  g_use_legacy_font_mime_types = false;

  g_kax_tracks = std::make_unique<libmatroska::KaxTracks>();
  g_cluster_helper = std::make_unique<cluster_helper_c>();
}

void install_handlers(mtx_context *context) {
  set_mxmsg_handler(MXMSG_INFO, [context](unsigned int, std::string const &message) {
    if (context->log_callback)
      context->log_callback(context->log_userdata, MTX_LOG_INFO, message.c_str());
  });

  set_mxmsg_handler(MXMSG_WARNING, [context](unsigned int, std::string const &message) {
    if (context->log_callback)
      context->log_callback(context->log_userdata, MTX_LOG_WARNING, message.c_str());
  });

  set_mxmsg_handler(MXMSG_ERROR, [context](unsigned int, std::string const &message) {
    context->last_error = message;
    throw api_error_x{message};
  });
}

void restore_default_handlers() {
  init_common_output(true);
}

int fail(mtx_context *context, int code, std::string message) {
  if (context)
    context->last_error = std::move(message);
  return code;
}

filelist_cptr input_file(mtx_merge *merge, mtx_input const &input) {
  if (!merge || input.index >= g_files.size())
    return {};
  return g_files[input.index];
}

track_info_c *track_info(mtx_merge *merge, mtx_track const &track) {
  auto file = input_file(merge, mtx_input{track.input_index});
  if (!file || !file->ti)
    return nullptr;
  return file->ti.get();
}

int ensure_identified(mtx_merge *merge, filelist_cptr const &file);

bool valid_track(mtx_merge *merge, mtx_track const &track) {
  auto file = input_file(merge, mtx_input{track.input_index});
  if (!file || !file->reader)
    return false;
  if (ensure_identified(merge, file) != MTX_OK)
    return false;

  for (auto const &t : file->reader->get_id_results_tracks()) {
    if (t.id == track.track_id)
      return true;
  }
  return false;
}

int finalize_and_execute(mtx_merge *merge) {
  if (!merge->headers_read) {
    read_file_headers();
    merge->headers_read = true;
  }

  for (auto const &file : g_files) {
    if (file && file->reader && file->ti)
      file->reader->set_track_info(*file->ti);
  }

  create_packetizers();
  check_track_id_validity();
  check_append_mapping();
  check_split_support();
  calc_attachment_sizes();
  calc_max_chapter_size();

  create_next_output_file();
  main_loop();
  finish_file(true);

  merge->executed = true;
  return MTX_OK;
}

nlohmann::json const *find_property(mtx::id::verbose_info_t const &info, std::string const &name) {
  for (auto const &item : info) {
    if (item.first == name)
      return &item.second;
  }
  return nullptr;
}

int ensure_identified(mtx_merge *merge, filelist_cptr const &file) {
  if (!merge || !file || !file->reader)
    return MTX_ERROR_INVALID_ARG;
  if (!merge->headers_read) {
    try {
      read_file_headers();
      merge->headers_read = true;
    } catch (api_error_x const &ex) {
      return fail(merge->context, MTX_ERROR_EXCEPTION, ex.what());
    } catch (std::exception const &ex) {
      return fail(merge->context, MTX_ERROR_EXCEPTION, ex.what());
    }
  }
  file->reader->run_identify();
  return MTX_OK;
}

nlohmann::json build_reader_json(generic_reader_c const &reader, std::string const &filename) {
  auto verbose_info_to_object = [](mtx::id::verbose_info_t const &verbose_info) -> nlohmann::json {
    auto object = nlohmann::json{};
    for (auto const &property : verbose_info)
      object[property.first] = property.second;
    return object.is_null() ? nlohmann::json::object() : object;
  };

  auto const &c_res = reader.get_id_results_container();
  auto const &tracks = reader.get_id_results_tracks();

  auto json = nlohmann::json{
    { "identification_format_version", ID_JSON_FORMAT_VERSION },
    { "file_name",                     filename },
    { "tracks",                        nlohmann::json::array() },
    { "container", {
        { "recognized", true },
        { "supported",  true },
        { "type",       c_res.info },
        { "properties", verbose_info_to_object(c_res.verbose_info) },
      } },
  };

  for (auto const &result : tracks) {
    json["tracks"] += nlohmann::json{
      { "id",         result.id },
      { "type",       result.type },
      { "codec",      result.info },
      { "properties", verbose_info_to_object(result.verbose_info) },
    };
  }

  return json;
}

}

extern "C" {

int MTX_API_CALL mtx_api_version(void) {
  return MTX_API_VERSION_MAJOR;
}

int MTX_API_CALL mtx_get_version(mtx_version_info_t *info) {
  if (!info)
    return MTX_ERROR_INVALID_ARG;
  static std::string s_ver = get_current_version().to_string();
  info->major = MTX_API_VERSION_MAJOR;
  info->minor = MTX_API_VERSION_MINOR;
  info->patch = MTX_API_VERSION_PATCH;
  info->abi_revision = 1;
  info->mkvtoolnix_version = s_ver.c_str();
  info->build_date = __DATE__ " " __TIME__;
  return MTX_OK;
}

int MTX_API_CALL mtx_init(uint32_t expected_major_version) {
  if (expected_major_version != MTX_API_VERSION_MAJOR)
    return MTX_ERROR_UNSUPPORTED;
  init_common();
  return MTX_OK;
}

mtx_context_t *MTX_API_CALL mtx_context_create(void) {
  std::lock_guard<std::mutex> lock{s_engine_mutex};

  if (s_active_context)
    return nullptr;

  try {
    init_common();
    auto *context = new mtx_context;
    reset_merge_state();
    install_handlers(context);
    s_active_context = context;
    return context;
  } catch (...) {
    return nullptr;
  }
}

void MTX_API_CALL mtx_context_destroy(mtx_context_t *context) {
  if (!context)
    return;

  std::lock_guard<std::mutex> lock{s_engine_mutex};
  if (s_active_context != context)
    return;

  cleanup();
  s_active_context = nullptr;
  restore_default_handlers();
  delete context;
}

const char *MTX_API_CALL mtx_context_last_error(mtx_context_t const *context) {
  return context ? context->last_error.c_str() : "invalid context";
}

void MTX_API_CALL mtx_context_set_log_callback(mtx_context_t *context, mtx_log_callback callback, void *userdata) {
  if (!context)
    return;
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  context->log_callback = callback;
  context->log_userdata = userdata;
}

mtx_merge_t *MTX_API_CALL mtx_merge_create(mtx_context_t *context) {
  if (!context || context != s_active_context)
    return nullptr;

  std::lock_guard<std::mutex> lock{s_engine_mutex};
  try {
    reset_merge_state();
    auto *merge = new mtx_merge;
    merge->context = context;
    return merge;
  } catch (...) {
    return nullptr;
  }
}

void MTX_API_CALL mtx_merge_destroy(mtx_merge_t *merge) {
  if (!merge)
    return;
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  cleanup();
  delete merge;
}

int MTX_API_CALL mtx_merge_set_output(mtx_merge_t *merge, const char *filename) {
  if (!merge || !filename || !*filename)
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid output filename");
  if (merge->headers_read)
    return fail(merge->context, MTX_ERROR_INVALID_STATE, "Output cannot be changed after prepare()");
  g_outfile = filename;
  return MTX_OK;
}

int MTX_API_CALL mtx_merge_set_title(mtx_merge_t *merge, const char *title) {
  if (!merge || !title)
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid title");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  g_segment_title = title;
  g_segment_title_set = true;
  return MTX_OK;
}

int MTX_API_CALL mtx_merge_set_default_language(mtx_merge_t *merge, const char *language) {
  if (!merge || !language)
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid language");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  auto parsed = mtx::bcp47::language_c::parse(language);
  if (!parsed.is_valid())
    return fail(merge->context, MTX_ERROR_INVALID_ARG, "Invalid BCP 47 language: " + std::string{language});
  g_default_language = parsed;
  return MTX_OK;
}

int MTX_API_CALL mtx_merge_set_cluster_max_duration(mtx_merge_t *merge, int64_t max_ms) {
  if (!merge || max_ms <= 0)
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid cluster duration");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  g_max_ns_per_cluster = max_ms * 1000000ll;
  return MTX_OK;
}

int MTX_API_CALL mtx_merge_set_track_statistics_tags(mtx_merge_t *merge, int enable) {
  if (!merge)
    return MTX_ERROR_INVALID_ARG;
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  g_no_track_statistics_tags = !enable;
  return MTX_OK;
}

int MTX_API_CALL mtx_merge_set_deterministic(mtx_merge_t *merge, int enable) {
  if (!merge)
    return MTX_ERROR_INVALID_ARG;
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  g_deterministic = enable != 0;
  return MTX_OK;
}

void MTX_API_CALL mtx_merge_set_progress_callback(mtx_merge_t *merge, mtx_progress_callback callback, void *userdata) {
  if (!merge)
    return;
  merge->progress_callback = callback;
  merge->progress_userdata = userdata;
}

int MTX_API_CALL mtx_merge_add_input(mtx_merge_t *merge, const char *filename, mtx_input_t **result) {
  if (!merge || !filename || !*filename || !result)
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid input arguments");
  if (merge->headers_read)
    return fail(merge->context, MTX_ERROR_INVALID_STATE, "Inputs cannot be added after prepare()");

  try {
    auto file = std::make_shared<filelist_t>();
    file->name = filename;
    file->all_names.push_back(filename);
    file->id = g_files.size();
    file->ti = std::make_unique<track_info_c>();
    file->ti->m_fname = filename;
    file->reader = probe_file_format(*file);
    if (!file->reader)
      return fail(merge->context, MTX_ERROR_UNSUPPORTED, "The input file could not be recognized: " + std::string{filename});

    g_files.push_back(file);
    auto inp = std::make_unique<mtx_input_t>();
    inp->index = static_cast<uint32_t>(file->id);
    *result = inp.get();
    merge->inputs.push_back(std::move(inp));
    return MTX_OK;
  } catch (std::exception const &ex) {
    return fail(merge->context, MTX_ERROR_EXCEPTION, ex.what());
  }
}

int MTX_API_CALL mtx_merge_prepare(mtx_merge_t *merge) {
  if (!merge)
    return MTX_ERROR_INVALID_ARG;
  if (merge->headers_read)
    return MTX_OK;
  if (g_outfile.empty())
    return fail(merge->context, MTX_ERROR_INVALID_ARG, "No output file has been configured");
  if (g_files.empty())
    return fail(merge->context, MTX_ERROR_INVALID_ARG, "No input files have been configured");

  std::lock_guard<std::mutex> lock{s_engine_mutex};
  try {
    read_file_headers();
    merge->headers_read = true;
    return MTX_OK;
  } catch (api_error_x const &ex) {
    return fail(merge->context, MTX_ERROR_EXCEPTION, ex.what());
  } catch (std::exception const &ex) {
    return fail(merge->context, MTX_ERROR_EXCEPTION, ex.what());
  }
}

int MTX_API_CALL mtx_merge_cancel(mtx_merge_t *merge) {
  if (!merge)
    return MTX_ERROR_INVALID_ARG;
  g_merge_cancel_requested.store(true, std::memory_order_relaxed);
  return MTX_OK;
}

int MTX_API_CALL mtx_merge_execute(mtx_merge_t *merge) {
  if (!merge)
    return MTX_ERROR_INVALID_ARG;
  if (merge->executed)
    return fail(merge->context, MTX_ERROR_INVALID_STATE, "The merge has already been executed");

  auto rc = mtx_merge_prepare(merge);
  if (rc != MTX_OK)
    return rc;

  std::lock_guard<std::mutex> lock{s_engine_mutex};
  if (merge->progress_callback) {
    g_merge_progress_callback = [merge](int pct, int64_t cur_ts, int64_t tot_dur, int64_t bytes_wr) {
      mtx_progress_info_t p;
      p.percentage = pct;
      p.current_timestamp_ns = cur_ts;
      p.total_duration_ns = tot_dur;
      p.bytes_written = bytes_wr;
      merge->progress_callback(merge->progress_userdata, &p);
    };
  } else {
    g_merge_progress_callback = nullptr;
  }

  if (g_merge_cancel_requested.load(std::memory_order_relaxed)) {
    if (!g_outfile.empty()) {
      std::error_code ec;
      std::filesystem::remove(std::filesystem::u8path(g_outfile), ec);
    }
    return fail(merge->context, MTX_ERROR_CANCELLED, "Operation cancelled by user");
  }

  try {
    return finalize_and_execute(merge);
  } catch (mtx::merge_cancelled_x const &) {
    force_close_output_file();
    if (!g_outfile.empty()) {
      std::error_code ec;
      std::filesystem::remove(std::filesystem::u8path(g_outfile), ec);
    }
    return fail(merge->context, MTX_ERROR_CANCELLED, "Operation cancelled by user");
  } catch (api_error_x const &ex) {
    force_close_output_file();
    return fail(merge->context, MTX_ERROR_EXCEPTION, ex.what());
  } catch (std::exception const &ex) {
    force_close_output_file();
    return fail(merge->context, MTX_ERROR_EXCEPTION, ex.what());
  }
}

size_t MTX_API_CALL mtx_input_get_track_count(mtx_merge_t *merge, mtx_input_t *input) {
  if (!merge || !input)
    return 0;
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  auto file = input_file(merge, *input);
  if (!file || !file->reader)
    return 0;
  if (ensure_identified(merge, file) != MTX_OK)
    return 0;
  return file->reader->get_id_results_tracks().size();
}

int MTX_API_CALL mtx_input_get_track(mtx_merge_t *merge, mtx_input_t *input, size_t index, mtx_track_t **result) {
  if (!merge || !input || !result)
    return MTX_ERROR_INVALID_ARG;
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  auto file = input_file(merge, *input);
  if (!file || !file->reader)
    return MTX_ERROR_INVALID_ARG;
  if (ensure_identified(merge, file) != MTX_OK)
    return MTX_ERROR_INVALID_ARG;
  auto const &tracks = file->reader->get_id_results_tracks();
  if (index >= tracks.size())
    return MTX_ERROR_INVALID_ARG;

  auto trk = std::make_unique<mtx_track_t>();
  trk->input_index = input->index;
  trk->track_id = tracks[index].id;
  *result = trk.get();
  merge->tracks.push_back(std::move(trk));
  return MTX_OK;
}

int64_t MTX_API_CALL mtx_track_get_id(mtx_track_t const *track) {
  return track ? track->track_id : -1;
}

int MTX_API_CALL mtx_track_get_type(mtx_merge_t *merge, mtx_track_t const *track, char *buf, size_t buf_size) {
  if (!merge || !track || !buf || buf_size == 0)
    return MTX_ERROR_INVALID_ARG;
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  auto file = input_file(merge, mtx_input{track->input_index});
  if (!file || !file->reader)
    return MTX_ERROR_INVALID_ARG;
  if (ensure_identified(merge, file) != MTX_OK)
    return MTX_ERROR_INVALID_ARG;

  for (auto const &t : file->reader->get_id_results_tracks()) {
    if (t.id == track->track_id) {
      strncpy_s(buf, buf_size, t.type.c_str(), _TRUNCATE);
      return MTX_OK;
    }
  }
  return MTX_ERROR_INVALID_ARG;
}

int MTX_API_CALL mtx_track_get_codec(mtx_merge_t *merge, mtx_track_t const *track, char *buf, size_t buf_size) {
  if (!merge || !track || !buf || buf_size == 0)
    return MTX_ERROR_INVALID_ARG;
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  auto file = input_file(merge, mtx_input{track->input_index});
  if (!file || !file->reader)
    return MTX_ERROR_INVALID_ARG;
  if (ensure_identified(merge, file) != MTX_OK)
    return MTX_ERROR_INVALID_ARG;

  for (auto const &t : file->reader->get_id_results_tracks()) {
    if (t.id == track->track_id) {
      strncpy_s(buf, buf_size, t.info.c_str(), _TRUNCATE);
      return MTX_OK;
    }
  }
  return MTX_ERROR_INVALID_ARG;
}

int MTX_API_CALL mtx_track_set_language(mtx_merge_t *merge, mtx_track_t *track, const char *language) {
  if (!language || !merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track or language");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  auto parsed = mtx::bcp47::language_c::parse(language);
  if (!parsed.is_valid())
    return fail(merge->context, MTX_ERROR_INVALID_ARG, "Invalid BCP 47 language: " + std::string{language});
  track_info(merge, *track)->m_languages[track->track_id] = parsed;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_name(mtx_merge_t *merge, mtx_track_t *track, const char *name) {
  if (!name || !merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track or name");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  track_info(merge, *track)->m_track_names[track->track_id] = name;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_default(mtx_merge_t *merge, mtx_track_t *track, int enabled) {
  if (!merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  track_info(merge, *track)->m_default_track_flags[track->track_id] = enabled != 0;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_forced(mtx_merge_t *merge, mtx_track_t *track, int enabled) {
  if (!merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  track_info(merge, *track)->m_forced_track_flags[track->track_id] = enabled != 0;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_enabled(mtx_merge_t *merge, mtx_track_t *track, int enabled) {
  if (!merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  track_info(merge, *track)->m_enabled_track_flags[track->track_id] = enabled != 0;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_hearing_impaired(mtx_merge_t *merge, mtx_track_t *track, int enabled) {
  if (!merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  track_info(merge, *track)->m_hearing_impaired_flags[track->track_id] = enabled != 0;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_visual_impaired(mtx_merge_t *merge, mtx_track_t *track, int enabled) {
  if (!merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  track_info(merge, *track)->m_visual_impaired_flags[track->track_id] = enabled != 0;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_original(mtx_merge_t *merge, mtx_track_t *track, int enabled) {
  if (!merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  track_info(merge, *track)->m_original_flags[track->track_id] = enabled != 0;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_commentary(mtx_merge_t *merge, mtx_track_t *track, int enabled) {
  if (!merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  track_info(merge, *track)->m_commentary_flags[track->track_id] = enabled != 0;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_delay(mtx_merge_t *merge, mtx_track_t *track, int64_t delay_ms) {
  if (!merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  track_info(merge, *track)->m_timestamp_syncs[track->track_id].displacement = delay_ms * 1000000ll;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_sync(mtx_merge_t *merge, mtx_track_t *track, int64_t delay_ms, double stretch_factor) {
  if (!merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  auto &sync = track_info(merge, *track)->m_timestamp_syncs[track->track_id];
  sync.displacement = delay_ms * 1000000ll;
  if (stretch_factor > 0.0) {
    sync.factor = mtx_mp_rational_t{static_cast<int64_t>(std::round(stretch_factor * 1000000000.0)), 1000000000ll};
  }
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_display_dimensions(mtx_merge_t *merge, mtx_track_t *track, uint32_t width, uint32_t height) {
  if (!merge || !track || !valid_track(merge, *track) || width == 0 || height == 0)
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid dimensions");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  auto &disp = track_info(merge, *track)->m_display_properties[track->track_id];
  disp.width = static_cast<int>(width);
  disp.height = static_cast<int>(height);
  disp.ar_factor = false;
  disp.aspect_ratio = 0;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_display_aspect_ratio(mtx_merge_t *merge, mtx_track_t *track, double aspect_ratio) {
  if (!merge || !track || !valid_track(merge, *track) || aspect_ratio <= 0.0)
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid aspect ratio");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  auto &disp = track_info(merge, *track)->m_display_properties[track->track_id];
  disp.aspect_ratio = aspect_ratio;
  disp.ar_factor = false;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_cropping(mtx_merge_t *merge, mtx_track_t *track, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom) {
  if (!merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  track_info(merge, *track)->m_pixel_crop_list[track->track_id] = pixel_crop_t(static_cast<int>(left), static_cast<int>(top), static_cast<int>(right), static_cast<int>(bottom));
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_compression(mtx_merge_t *merge, mtx_track_t *track, int compression_mode) {
  if (!merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  if (compression_mode == MTX_COMPRESSION_NONE)
    track_info(merge, *track)->m_compression_list[track->track_id] = COMPRESSION_NONE;
  else if (compression_mode == MTX_COMPRESSION_ZLIB)
    track_info(merge, *track)->m_compression_list[track->track_id] = COMPRESSION_ZLIB;
  return MTX_OK;
}

int MTX_API_CALL mtx_track_set_cues(mtx_merge_t *merge, mtx_track_t *track, int cue_mode) {
  if (!merge || !track || !valid_track(merge, *track))
    return fail(merge ? merge->context : nullptr, MTX_ERROR_INVALID_ARG, "Invalid track");
  std::lock_guard<std::mutex> lock{s_engine_mutex};
  if (cue_mode == MTX_CUES_NONE)
    track_info(merge, *track)->m_cue_creations[track->track_id] = CUE_STRATEGY_NONE;
  else if (cue_mode == MTX_CUES_IFRAMES)
    track_info(merge, *track)->m_cue_creations[track->track_id] = CUE_STRATEGY_IFRAMES;
  else if (cue_mode == MTX_CUES_ALL)
    track_info(merge, *track)->m_cue_creations[track->track_id] = CUE_STRATEGY_ALL;
  return MTX_OK;
}

int MTX_API_CALL mtx_input_get_file_info(mtx_merge_t *merge, mtx_input_t *input, mtx_file_info_t *result) {
  if (!merge || !input || !result)
    return MTX_ERROR_INVALID_ARG;

  std::lock_guard<std::mutex> lock{s_engine_mutex};
  auto file = input_file(merge, *input);
  if (!file || !file->reader)
    return fail(merge->context, MTX_ERROR_INVALID_ARG, "Invalid input handle");

  int rc = ensure_identified(merge, file);
  if (rc != MTX_OK)
    return rc;

  memset(result, 0, sizeof(*result));
  auto const &c_res = file->reader->get_id_results_container();
  strncpy_s(result->container_format, sizeof(result->container_format), c_res.info.c_str(), _TRUNCATE);
  result->track_count = static_cast<uint32_t>(file->reader->get_id_results_tracks().size());

  if (auto prop = find_property(c_res.verbose_info, mtx::id::duration)) {
    if (prop->is_number())
      result->duration_ns = prop->get<int64_t>();
  }

  return MTX_OK;
}

int MTX_API_CALL mtx_input_get_track_info(mtx_merge_t *merge, mtx_input_t *input, size_t index, mtx_track_info_t *result) {
  if (!merge || !input || !result)
    return MTX_ERROR_INVALID_ARG;

  std::lock_guard<std::mutex> lock{s_engine_mutex};
  auto file = input_file(merge, *input);
  if (!file || !file->reader)
    return fail(merge->context, MTX_ERROR_INVALID_ARG, "Invalid input handle");

  int rc = ensure_identified(merge, file);
  if (rc != MTX_OK)
    return rc;

  auto const &tracks = file->reader->get_id_results_tracks();
  if (index >= tracks.size())
    return fail(merge->context, MTX_ERROR_INVALID_ARG, "Track index out of range");

  auto const &t_res = tracks[index];
  memset(result, 0, sizeof(*result));
  result->track_id = t_res.id;
  strncpy_s(result->type, sizeof(result->type), t_res.type.c_str(), _TRUNCATE);
  strncpy_s(result->codec, sizeof(result->codec), t_res.info.c_str(), _TRUNCATE);

  if (auto prop = find_property(t_res.verbose_info, mtx::id::pixel_dimensions)) {
    if (prop->is_string()) {
      unsigned int w = 0, h = 0;
      if (sscanf_s(prop->get<std::string>().c_str(), "%ux%u", &w, &h) == 2) {
        result->pixel_width = w;
        result->pixel_height = h;
      }
    }
  }

  if (auto prop = find_property(t_res.verbose_info, mtx::id::display_dimensions)) {
    if (prop->is_string()) {
      unsigned int dw = 0, dh = 0;
      if (sscanf_s(prop->get<std::string>().c_str(), "%ux%u", &dw, &dh) == 2) {
        result->display_width = dw;
        result->display_height = dh;
      }
    }
  }

  if (auto prop = find_property(t_res.verbose_info, mtx::id::default_duration)) {
    if (prop->is_number()) {
      double ns = prop->get<double>();
      if (ns > 0.0)
        result->fps = 1000000000.0 / ns;
    }
  }

  if (auto prop = find_property(t_res.verbose_info, mtx::id::audio_channels)) {
    if (prop->is_number())
      result->audio_channels = prop->get<uint32_t>();
  }

  if (auto prop = find_property(t_res.verbose_info, mtx::id::audio_sampling_frequency)) {
    if (prop->is_number())
      result->audio_sampling_frequency = prop->get<uint32_t>();
  }

  if (auto prop = find_property(t_res.verbose_info, mtx::id::audio_bits_per_sample)) {
    if (prop->is_number())
      result->audio_bits_per_sample = prop->get<uint32_t>();
  }

  if (auto prop = find_property(t_res.verbose_info, mtx::id::language)) {
    if (prop->is_string())
      strncpy_s(result->language, sizeof(result->language), prop->get<std::string>().c_str(), _TRUNCATE);
  }

  if (auto prop = find_property(t_res.verbose_info, mtx::id::language_ietf)) {
    if (prop->is_string())
      strncpy_s(result->language_ietf, sizeof(result->language_ietf), prop->get<std::string>().c_str(), _TRUNCATE);
  }

  if (auto prop = find_property(t_res.verbose_info, mtx::id::track_name)) {
    if (prop->is_string())
      strncpy_s(result->name, sizeof(result->name), prop->get<std::string>().c_str(), _TRUNCATE);
  }

  if (auto prop = find_property(t_res.verbose_info, mtx::id::default_track)) {
    if (prop->is_boolean())
      result->is_default = prop->get<bool>() ? 1 : 0;
  }

  if (auto prop = find_property(t_res.verbose_info, mtx::id::forced_track)) {
    if (prop->is_boolean())
      result->is_forced = prop->get<bool>() ? 1 : 0;
  }

  if (auto prop = find_property(t_res.verbose_info, mtx::id::enabled_track)) {
    if (prop->is_boolean())
      result->is_enabled = prop->get<bool>() ? 1 : 0;
  }

  return MTX_OK;
}

const char * MTX_API_CALL mtx_input_get_json_info(mtx_merge_t *merge, mtx_input_t *input) {
  if (!merge || !input)
    return nullptr;

  std::lock_guard<std::mutex> lock{s_engine_mutex};
  auto file = input_file(merge, *input);
  if (!file || !file->reader) {
    fail(merge->context, MTX_ERROR_INVALID_ARG, "Invalid input handle");
    return nullptr;
  }

  int rc = ensure_identified(merge, file);
  if (rc != MTX_OK)
    return nullptr;

  auto json = build_reader_json(*file->reader, file->name);
  merge->context->last_json_info = mtx::json::dump(json, 2);
  return merge->context->last_json_info.c_str();
}

}
