#include "ziplain.h"

#include <endian.h>
#include <string.h>
#include <zlib.h>

#include <cstdio>
#include <memory>
#include <vector>

// Compression currently not implemented. Follow this macro to find all the
// places to change and then remove this macro :)
#define COMPRESSION_IMPLEMENTED 0

namespace ziplain {
ByteSource MemoryByteSource(std::string_view input) {
  auto is_called = std::make_shared<bool>(false);
  return [is_called, input]() -> std::string_view {
    if (!*is_called) {
      *is_called = true;
      return input;
    }
    return {};
  };
}

ByteSource FileByteSource(const char *filename) {
  FILE *f = fopen(filename, "rb");
  if (!f) return nullptr;
  struct State {
    State(FILE *f) : file(f) {}
    ~State() { fclose(file); }
    FILE *file;
    char buffer[4096];
  };
  auto state = std::make_shared<State>(f);  // capture req. copy-constructable
  return [state]() -> std::string_view {
    size_t r = fread(state->buffer, 1, sizeof(State::buffer), state->file);
    return {state->buffer, r};
  };
}

// Header writer, taking care of little-endianness writing of fields.
namespace {
class HeaderWriter {
 public:
  HeaderWriter(char *buffer) : begin_(buffer), pos_(buffer) {}

  HeaderWriter &AddInt16(uint16_t value) {
    uint16_t le = htole16(value);  // NOP on most platforms
    memcpy(pos_, &le, 2);
    pos_ += 2;
    return *this;
  }
  HeaderWriter &AddInt32(uint32_t value) {
    uint32_t le = htole32(value);  // NOP on most platforms
    memcpy(pos_, &le, 4);
    pos_ += 4;
    return *this;
  }
  HeaderWriter &AddLiteral(std::string_view str) {
    memcpy(pos_, str.data(), str.size());
    pos_ += str.size();
    return *this;
  }

  bool Write(const ByteSink &out) {
    return out({begin_, static_cast<size_t>(pos_ - begin_)});
  }

  void Append(std::vector<char> *buf) {
    buf->insert(buf->end(), begin_, static_cast<const char *>(pos_));
  }

 private:
  const char *begin_;
  char *pos_;
};
}  // namespace

struct Encoder::Impl {
  static constexpr int16_t kPkZipVersion = 20;  // 2.0, pretty basic.

  Impl(int compression_level, ByteSink out)
      : compression_level_(compression_level),
        delegate_write_(std::move(out)),
        out_([this](std::string_view s) {
          output_file_offset_ += s.size();  // Keep track of offsets.
          return delegate_write_(s);
        }) {}

  bool AddFile(std::string_view filename, ByteSource content_generator) {
    if (is_finished_) return false;  // Can't add more files.
    ++file_count_;
    const size_t start_offset = output_file_offset_;

    const uint16_t mod_time = 0;  // TODO: accept time_t and convert ?
    const uint16_t mod_date = 0;

    bool success =  // Assemble local file header
      HeaderWriter(scratch_space)
        .AddLiteral("PK\x03\x04")
        .AddInt16(kPkZipVersion)  // Minimum version needed
        .AddInt16(0x08)           // Flags. Sizes and CRC in data descriptor.
#if COMPRESSION_IMPLEMENTED
        .AddInt16(compression_level_ <= 0 ? 0 : 8)
#else
        .AddInt16(0)  // Currently only no compression.
#endif
        .AddInt16(mod_time)
        .AddInt16(mod_date)
        .AddInt32(0)  // CRC32. Known later.
        .AddInt32(0)  // Compressed size: known later.
        .AddInt32(0)  // Uncompressed size: known later.
        .AddInt16(filename.length())
        .AddInt16(0)  // Extra field length
        .AddLiteral(filename)
        .Write(out_);
    if (!success) return false;

    // Copy out the data.
    uint32_t crc = 0;
    std::string_view chunk;
    size_t size_before_compression = 0;
    size_t size_after_compression = 0;
    while (!(chunk = content_generator()).empty()) {
      crc = crc32(crc, (const Bytef *)chunk.data(), chunk.size());
      size_before_compression += chunk.size();
#if COMPRESSION_IMPLEMENTED
#     error "TODO: Compression not implemented"
#endif
      out_(chunk);  // TODO: gracefully deal with unsuccessful write
      size_after_compression += chunk.size();
    }

    success =  // Assemble Data Descriptor after file with known CRC and size.
      HeaderWriter(scratch_space)
        .AddInt32(crc)
        .AddInt32(size_after_compression)
        .AddInt32(size_before_compression)
        .Write(out_);

    // Append directory file header entry to be written in Finish()
    HeaderWriter(scratch_space)
      .AddLiteral("PK\x01\x02")
      .AddInt16(kPkZipVersion)  // Our Version
      .AddInt16(kPkZipVersion)  // Readable by version
      .AddInt16(0x08)           // Flag
#if COMPRESSION_IMPLEMENTED
      .AddInt16(compression_level_ <= 0 ? 0 : 8)
#else
      .AddInt16(0)
#endif
      .AddInt16(mod_time)
      .AddInt16(mod_date)
      .AddInt32(crc)  // CRC32
      .AddInt32(size_after_compression)
      .AddInt32(size_before_compression)
      .AddInt16(filename.length())
      .AddInt16(0)  // Extra field length
      .AddInt16(0)  // file comment length
      .AddInt16(0)  // disk number
      .AddInt16(0)  // Internal file attr
      .AddInt32(0)  // External file attr
      .AddInt32(start_offset)
      .AddLiteral(filename)
      .Append(&central_dir_data_);

    return success;
  }

  bool Finish() {
    if (is_finished_) return false;
    is_finished_ = true;
    const size_t start_offset = output_file_offset_;
    if (!out_({central_dir_data_.data(), central_dir_data_.size()})) {
      return false;
    }

    // End of central directory record
    constexpr std::string_view comment("Created with ziplain");
    return HeaderWriter(scratch_space)
      .AddLiteral("PK\x05\x06")  // End of central directory signature
      .AddInt16(0)               // our disk number
      .AddInt16(0)               // disk where it all starts
      .AddInt16(file_count_)     // Number of directory records on this disk
      .AddInt16(file_count_)     // ... and overall
      .AddInt32(central_dir_data_.size())
      .AddInt32(start_offset)
      .AddInt16(comment.length())
      .AddLiteral(comment)
      .Write(out_);
  }

  const int compression_level_;
  const ByteSink delegate_write_;
  const ByteSink out_;

  size_t file_count_ = 0;
  size_t output_file_offset_ = 0;
  std::vector<char> central_dir_data_;
  bool is_finished_ = false;
  char scratch_space[1 << 20];  // to assemble headers and compression data
};

Encoder::Encoder(int compression_level, ByteSink out)
    : impl_(new Impl(compression_level, std::move(out))) {}

Encoder::~Encoder() {}

bool Encoder::AddFile(std::string_view filename, ByteSource content) {
  return impl_->AddFile(filename, std::move(content));
}
bool Encoder::Finish() { return impl_->Finish(); }
}  // namespace ziplain
