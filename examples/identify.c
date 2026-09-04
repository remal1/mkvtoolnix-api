#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mkvtoolnix-api/c_api.h"

static void format_duration(int64_t duration_ns, char *buf, size_t buf_size) {
  if (duration_ns <= 0) {
    snprintf(buf, buf_size, "Ismeretlen");
    return;
  }
  double total_seconds = (double)duration_ns / 1000000000.0;
  int hours = (int)(total_seconds / 3600);
  int minutes = (int)((total_seconds - (hours * 3600)) / 60);
  double seconds = total_seconds - (hours * 3600) - (minutes * 60);

  if (hours > 0) {
    snprintf(buf, buf_size, "%02d:%02d:%06.3f (%ld mp)", hours, minutes, seconds, (long)total_seconds);
  } else {
    snprintf(buf, buf_size, "%02d:%06.3f (%ld mp)", minutes, seconds, (long)total_seconds);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Hasznalat: %s <videofajl> [--json]\n", argv[0]);
    return 1;
  }

  const char *filename = argv[1];
  int print_json = (argc >= 3 && strcmp(argv[2], "--json") == 0);

  mtx_context *ctx = mtx_context_create();
  if (!ctx) {
    fprintf(stderr, "Hiba: Nem sikerult letrehozni az MKVToolNix kontextust.\n");
    return 2;
  }

  mtx_merge *merge = mtx_merge_create(ctx);
  if (!merge) {
    fprintf(stderr, "Hiba: Nem sikerult letrehozni a merge motort.\n");
    mtx_context_destroy(ctx);
    return 3;
  }

  mtx_input_t *input = NULL;
  int rc = mtx_merge_add_input(merge, filename, &input);
  if (rc != MTX_OK) {
    fprintf(stderr, "Hiba a fajl megnyitasakor: %s\n", mtx_context_last_error(ctx));
    mtx_merge_destroy(merge);
    mtx_context_destroy(ctx);
    return 4;
  }

  if (print_json) {
    const char *json = mtx_input_get_json_info(merge, input);
    if (json) {
      printf("%s\n", json);
    } else {
      fprintf(stderr, "Hiba a JSON kinyeresekor: %s\n", mtx_context_last_error(ctx));
    }
    mtx_merge_destroy(merge);
    mtx_context_destroy(ctx);
    return 0;
  }

  mtx_file_info file_info;
  rc = mtx_input_get_file_info(merge, input, &file_info);
  if (rc != MTX_OK) {
    fprintf(stderr, "Hiba a fajlinformaciok lekerdezese kozben: %s\n", mtx_context_last_error(ctx));
    mtx_merge_destroy(merge);
    mtx_context_destroy(ctx);
    return 5;
  }

  char duration_str[64];
  format_duration(file_info.duration_ns, duration_str, sizeof(duration_str));

  printf("================================================================================\n");
  printf(" Fajl: %s\n", filename);
  printf(" Kontener formatum: %s\n", file_info.container_format);
  printf(" Jatekido:          %s\n", duration_str);
  printf(" Savok szama:       %u\n", file_info.track_count);
  printf("================================================================================\n\n");

  for (size_t i = 0; i < file_info.track_count; i++) {
    mtx_track_info track;
    rc = mtx_input_get_track_info(merge, input, i, &track);
    if (rc != MTX_OK)
      continue;

    const char *type_hu = "Egyeb";
    if (strcmp(track.type, "video") == 0) type_hu = "Video";
    else if (strcmp(track.type, "audio") == 0) type_hu = "Audio";
    else if (strcmp(track.type, "subtitles") == 0) type_hu = "Felirat";

    printf("  [Sav #%zu | ID: %lld] Tipus: %s (%s)\n", i + 1, (long long)track.track_id, type_hu, track.type);
    printf("    Kodek:      %s\n", track.codec[0] ? track.codec : "Ismeretlen");
    
    if (track.language[0]) {
      printf("    Nyelv:      %s", track.language);
      if (track.language_ietf[0])
        printf(" (IETF / BCP 47: %s)", track.language_ietf);
      printf("\n");
    }

    if (track.name[0]) {
      printf("    Nev:        %s\n", track.name);
    }

    if (strcmp(track.type, "video") == 0) {
      if (track.pixel_width > 0 && track.pixel_height > 0) {
        printf("    Felbontas:  %u x %u", track.pixel_width, track.pixel_height);
        if (track.display_width > 0 && track.display_height > 0 &&
            (track.display_width != track.pixel_width || track.display_height != track.pixel_height)) {
          printf(" (Megjelenitett: %u x %u)", track.display_width, track.display_height);
        }
        printf("\n");
      }
      if (track.fps > 0.0) {
        printf("    Kepsebesseg:%.3f fps\n", track.fps);
      }
    } else if (strcmp(track.type, "audio") == 0) {
      if (track.audio_channels > 0) {
        printf("    Csatornak:  %u", track.audio_channels);
        if (track.audio_channels == 1) printf(" (Mono)");
        else if (track.audio_channels == 2) printf(" (Sztereo)");
        else if (track.audio_channels == 6) printf(" (5.1 Surround)");
        else if (track.audio_channels == 8) printf(" (7.1 Surround)");
        printf("\n");
      }
      if (track.audio_sampling_frequency > 0) {
        printf("    Mintavetel: %u Hz\n", track.audio_sampling_frequency);
      }
      if (track.audio_bits_per_sample > 0) {
        printf("    Bitmelyseg: %u bit\n", track.audio_bits_per_sample);
      }
    }

    printf("    Zaszlok:    Alapertelmezett: %s | Kenyszeritett: %s | Engedelyezve: %s\n",
           track.is_default ? "Igen" : "Nem",
           track.is_forced ? "Igen" : "Nem",
           track.is_enabled ? "Igen" : "Nem");
    printf("\n");
  }

  printf("================================================================================\n");

  mtx_merge_destroy(merge);
  mtx_context_destroy(ctx);
  return 0;
}
