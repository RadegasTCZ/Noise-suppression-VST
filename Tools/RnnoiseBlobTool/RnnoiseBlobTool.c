/* Copyright (c) 2023 Amazon */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* RnnoiseBlobTool - writes the RNNoise model 0a8755f8 weight blobs used by Noise Suppression.

   write_weights() is copied from upstream rnnoise src/write_weights.c (commit 70f1d25) with one
   change: the output file is opened in binary mode ("wb"; upstream's "w" corrupts the blob on
   Windows by translating '\n' to "\r\n"). Usage: RnnoiseBlobTool <output folder>
   Writes <folder>/rnnoise_standard.bin and <folder>/rnnoise_light.bin. Run by hand (build.bat);
   not part of the plugin build. */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "nnet.h"
#include "arch.h"
#include "common.h"

extern const WeightArray nsp_blob_arrays_standard[];
extern const WeightArray nsp_blob_arrays_light[];

static void write_weights(const WeightArray *list, FILE *fout)
{
  int i=0;
  unsigned char zeros[WEIGHT_BLOCK_SIZE] = {0};
  while (list[i].name != NULL) {
    WeightHead h;
    if (strlen(list[i].name) >= sizeof(h.name) - 1) {
      printf("[write_weights] warning: name %s too long\n", list[i].name);
    }
    memcpy(h.head, "DNNw", 4);
    h.version = WEIGHT_BLOB_VERSION;
    h.type = list[i].type;
    h.size = list[i].size;
    h.block_size = (h.size+WEIGHT_BLOCK_SIZE-1)/WEIGHT_BLOCK_SIZE*WEIGHT_BLOCK_SIZE;
    RNN_CLEAR(h.name, sizeof(h.name));
    strncpy(h.name, list[i].name, sizeof(h.name));
    h.name[sizeof(h.name)-1] = 0;
    celt_assert(sizeof(h) == WEIGHT_BLOCK_SIZE);
    fwrite(&h, 1, WEIGHT_BLOCK_SIZE, fout);
    fwrite(list[i].data, 1, h.size, fout);
    fwrite(zeros, 1, h.block_size-h.size, fout);
    i++;
  }
}

static int write_file(const char *folder, const char *name, const WeightArray *list)
{
  char path[1024];
  FILE *f;
  snprintf(path, sizeof(path), "%s/%s", folder, name);
  f = fopen(path, "wb");
  if (f == NULL) {
    fprintf(stderr, "cannot open %s\n", path);
    return 1;
  }
  write_weights(list, f);
  fclose(f);
  printf("wrote %s\n", path);
  return 0;
}

int main(int argc, char **argv)
{
  if (argc != 2) {
    fprintf(stderr, "usage: RnnoiseBlobTool <output folder>\n");
    return 2;
  }
  if (write_file(argv[1], "rnnoise_standard.bin", nsp_blob_arrays_standard)) return 1;
  if (write_file(argv[1], "rnnoise_light.bin", nsp_blob_arrays_light)) return 1;
  return 0;
}
