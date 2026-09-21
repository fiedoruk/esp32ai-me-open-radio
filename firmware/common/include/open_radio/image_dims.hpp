#pragma once

#include <cstddef>
#include <cstdint>

namespace open_radio {

// Width and height of a PNG or JPEG, read from the first bytes of the file.
//
// The device fetches station icons unattended at first configuration and has
// to compute a decode scale BEFORE decoding: LovyanGFX will happily draw a
// 512x512 icon into a 120x72 sprite, but only if it is told the scale, and
// the scale needs the source dimensions. Decoding twice to learn them would
// double the slowest step of first boot. Both headers put the dimensions
// within the first kilobyte, so this stays a pure function over bytes and is
// tested on the host with crafted headers.
struct ImageDimensions {
  bool valid = false;
  bool png = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

inline ImageDimensions parseImageDimensions(const std::uint8_t* data,
                                            std::size_t length) {
  ImageDimensions out;
  if (data == nullptr) return out;
  // PNG: 8-byte signature, then the IHDR chunk whose payload starts with
  // big-endian width and height at offsets 16 and 20.
  if (length >= 24 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
      data[3] == 'G') {
    out.png = true;
    out.width = (static_cast<std::uint32_t>(data[16]) << 24) |
                (static_cast<std::uint32_t>(data[17]) << 16) |
                (static_cast<std::uint32_t>(data[18]) << 8) | data[19];
    out.height = (static_cast<std::uint32_t>(data[20]) << 24) |
                 (static_cast<std::uint32_t>(data[21]) << 16) |
                 (static_cast<std::uint32_t>(data[22]) << 8) | data[23];
    out.valid = out.width > 0 && out.height > 0;
    return out;
  }
  // JPEG: walk the marker chain to the first start-of-frame. DHT (0xc4),
  // DAC (0xcc) and the RST markers reuse the 0xCn space without carrying a
  // frame header, so they are skipped explicitly.
  if (length >= 4 && data[0] == 0xff && data[1] == 0xd8) {
    std::size_t offset = 2;
    while (offset + 9 < length) {
      if (data[offset] != 0xff) return out;
      const std::uint8_t marker = data[offset + 1];
      const std::size_t segment =
          (static_cast<std::size_t>(data[offset + 2]) << 8) | data[offset + 3];
      if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 &&
          marker != 0xc8 && marker != 0xcc) {
        out.height = (static_cast<std::uint32_t>(data[offset + 5]) << 8) |
                     data[offset + 6];
        out.width = (static_cast<std::uint32_t>(data[offset + 7]) << 8) |
                    data[offset + 8];
        out.valid = out.width > 0 && out.height > 0;
        return out;
      }
      if (segment < 2) return out;
      offset += 2 + segment;
    }
  }
  return out;
}

// Walks the PNG chunk chain: every chunk's declared length (plus its 12 bytes
// of framing) must fit inside the buffer, and IEND must be reached. Decoders
// that skip unknown chunks by pointer arithmetic trust the declared length;
// a favicon fetched without transport integrity must not be allowed to move
// that pointer past the buffer (Codex F02, audit 2026-09-08). Pure function
// over bytes; the decoder's own checks still run afterwards.
inline bool pngChunksWellFormed(const std::uint8_t* data, std::size_t length) {
  if (data == nullptr || length < 8 + 12) return false;
  std::size_t offset = 8;
  while (offset + 12 <= length) {
    const std::uint32_t chunkLength = (static_cast<std::uint32_t>(data[offset]) << 24) |
                                      (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
                                      (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
                                      data[offset + 3];
    if (chunkLength > 0x7fffffffU) return false;
    if (chunkLength > length - offset - 12) return false;
    const bool isEnd = data[offset + 4] == 'I' && data[offset + 5] == 'E' &&
                       data[offset + 6] == 'N' && data[offset + 7] == 'D';
    offset += 12 + static_cast<std::size_t>(chunkLength);
    if (isEnd) return true;
  }
  return false;
}

}  // namespace open_radio
