#include "ziplain.h"

#include <stdio.h>

// Simple demo.
int main() {
  constexpr int kCompressionLevel = 9;
  const char *out_file = "test-output.zip";

  FILE *out = fopen(out_file, "wb");
  ziplain::Encoder zipper(kCompressionLevel, [out](std::string_view s) {
    return fwrite(s.data(), 1, s.size(), out) == s.size();
  });

  zipper.AddFile("ziplain.h", ziplain::FileByteSource("ziplain.h"));
  zipper.AddFile("ziplain.cc", ziplain::FileByteSource("ziplain.cc"));
  zipper.AddFile("readme.txt", ziplain::MemoryByteSource("Hello world!\n"));
  zipper.Finish();

  fclose(out);
  fprintf(stderr, "Wrote %s\n", out_file);
}
