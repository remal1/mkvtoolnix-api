#include <stdio.h>
#include "mkvtoolnix-api/c_api.h"

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s input output\n", argv[0]);
    return 2;
  }

  mtx_context_t *ctx = mtx_context_create();
  if (!ctx) return 3;

  mtx_merge_t *merge = mtx_merge_create(ctx);
  mtx_input_t *input = NULL;

  if (mtx_merge_set_output(merge, argv[2]) != MTX_OK ||
      mtx_merge_add_input(merge, argv[1], &input) != MTX_OK ||
      mtx_merge_execute(merge) != MTX_OK) {
    fprintf(stderr, "%s\n", mtx_context_last_error(ctx));
    mtx_merge_destroy(merge);
    mtx_context_destroy(ctx);
    return 4;
  }

  mtx_merge_destroy(merge);
  mtx_context_destroy(ctx);
  return 0;
}
