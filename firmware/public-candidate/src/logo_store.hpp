#pragma once

#include <HTTPClient.h>
#include <M5Unified.h>
#include <SPIFFS.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "open_radio/image_dims.hpp"
#include "open_radio/renderer.hpp"
#include "station_catalog.hpp"
#include "station_slots.hpp"

namespace open_radio::public_candidate {

// Fetches, decodes and stores the nine tile logos — on the device, once,
// unattended. This is the whole of the owner's rule made mechanism: the same
// binary everyone downloads produces the same tiles everywhere, with no
// hand-injected asset pack. Every URL the store will ever touch was compiled
// in from data that passed the same-day QC probe (200, PNG magic, sane
// dimensions); the phone never supplies an address.
//
// Runs in the window after Wi-Fi associates and before the Bluetooth stack
// exists — the only time this device has both internet and enough internal
// heap for TLS. PNG only: LovyanGFX gives PNG a float scale factor while JPEG
// only offers power-of-two divisions, and 69 of the 76 QC-verified icons plus
// all nine defaults are PNG. A JPEG icon keeps its monogram.
// A Stream that only accepts bytes, so HTTPClient's own body reader can be
// pointed at a fixed buffer. It exists because writeToStream() is the only
// route through that class which understands chunked transfer encoding, and
// it needs somewhere to write.
//
// Refuses to write past the end rather than truncating: a half-downloaded PNG
// that still carries a valid signature and header would decode into a torn
// image, which is worse than no icon because it looks deliberate.
class BufferSink : public Stream {
 public:
  BufferSink(std::uint8_t* buffer, std::size_t capacity)
      : buffer_(buffer), capacity_(capacity) {}

  std::size_t written() const { return written_; }
  bool overflowed() const { return overflowed_; }

  size_t write(std::uint8_t value) override { return write(&value, 1); }

  size_t write(const std::uint8_t* data, size_t length) override {
    if (overflowed_ || written_ + length > capacity_) {
      overflowed_ = true;
      return 0;
    }
    std::memcpy(buffer_ + written_, data, length);
    written_ += length;
    return length;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  std::uint8_t* buffer_;
  std::size_t capacity_;
  std::size_t written_ = 0;
  bool overflowed_ = false;
};

class LogoStore {
 public:
  static constexpr std::size_t kSlots = 9;
  // Square, cover-cropped. The tile slots are square and the owner's rule is
  // that the mark FILLS the block — a widescreen canvas with letterbox bars
  // shrank every icon twice (once into the canvas, once into the slot).
  static constexpr int kLogoWidth = 96;
  static constexpr int kLogoHeight = 96;
  // Measured verdict (reason=alloc in the field): a 256 KB per-fetch PSRAM
  // request survives about two fetches next to a live stream before
  // fragmentation denies it forever. Real icons are 1.6-22 KB; 96 KB is
  // ample and, allocated ONCE at begin() before the big audio buffers carve
  // up PSRAM, it can never fail mid-run again.
  static constexpr std::size_t kMaxDownloadBytes = 98304;
  // The fetch runs on its OWN low-priority task, never on the audio loop, so
  // the windows can be generous: broadcaster CDNs with slow TLS fronts get
  // the time they need and a stubborn icon costs nothing but its own wait.
  static constexpr std::uint32_t kFetchConnectTimeoutMs = 5000;
  static constexpr std::uint32_t kFetchBodyTimeoutMs = 8000;

  bool begin() {
    // Mount only — NEVER format here. begin() runs in setup() on loopTask,
    // which sits under a 30 s panic watchdog, and formatting the 3.4 MB
    // partition takes longer than that on slower flash chips. The watchdog
    // then kills the half-done format, the next boot starts it from scratch,
    // and the device loops forever before Wi-Fi exists (first field report:
    // factory-fresh Core2 v1.1, 2026-07-22). ensureReady() on the logo task
    // owns formatting instead.
    mounted_ = SPIFFS.begin(false);
    if (!mounted_) Serial.println("logo_store stage=mount result=defer-format");
    if (downloadBuffer_ == nullptr) {
      downloadBuffer_ = static_cast<std::uint8_t*>(heap_caps_malloc(
          kMaxDownloadBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (downloadBuffer_ == nullptr) {
        Serial.println("logo_store stage=buffer result=fail");
      }
    }
    return mounted_;
  }

  // The only place allowed to pay the multi-second SPIFFS.format() bill.
  // Runs on the logo fetch task: core 0, low priority, not subscribed to the
  // task watchdog, and only after the radio is already playing.
  bool ensureReady() {
    if (mounted_) return true;
    Serial.println("logo_store stage=format result=begin");
    const bool formatted = SPIFFS.format();
    mounted_ = formatted && SPIFFS.begin(false);
    Serial.printf("logo_store stage=format result=%s\n",
                  mounted_ ? "ok" : "fail");
    return mounted_;
  }

  // A factory device has no logos; a new build refetches them, because the
  // catalog (and with it the URLs) ships inside the build. Deleting the
  // stored files is bounded (18 small files) and stays far under the loop
  // watchdog, unlike the full-partition format this used to run. A virgin
  // filesystem that cannot mount holds nothing to delete — the logo task
  // formats it later via ensureReady().
  void clearStored() {
    if (!mounted_) begin();
    if (mounted_) {
      for (std::size_t slot = 0; slot < kSlots; ++slot) {
        char path[16];
        std::snprintf(path, sizeof(path), "/l%u.bin",
                      static_cast<unsigned>(slot));
        SPIFFS.remove(path);
        std::snprintf(path, sizeof(path), "/l%u.url",
                      static_cast<unsigned>(slot));
        SPIFFS.remove(path);
      }
    }
    attemptedMask_ = 0;
    for (auto& logo : logos_) logo = {};
  }

  const open_radio::ui::RuntimeStationLogo* logos() const { return logos_.data(); }

  void loadAll(const StationSlots& slots) {
    if (!mounted_) return;
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
      logos_[slot] = {};
      const char* url = faviconFor(slots, slot);
      if (url == nullptr || url[0] == '\0') continue;
      char path[16];
      std::snprintf(path, sizeof(path), "/l%u.bin", static_cast<unsigned>(slot));
      if (!sidecarMatches(slot, url)) continue;
      File file = SPIFFS.open(path, FILE_READ);
      if (!file) continue;
      const std::size_t expected =
          static_cast<std::size_t>(kLogoWidth) * kLogoHeight * 2;
      if (static_cast<std::size_t>(file.size()) != expected) {
        file.close();
        continue;
      }
      if (pixelStorage_[slot] == nullptr) {
        pixelStorage_[slot] = static_cast<std::uint16_t*>(
            heap_caps_malloc(expected, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      }
      if (pixelStorage_[slot] == nullptr) {
        file.close();
        continue;
      }
      const std::size_t got =
          file.read(reinterpret_cast<std::uint8_t*>(pixelStorage_[slot]),
                    expected);
      file.close();
      if (got != expected) continue;
      logos_[slot] = {pixelStorage_[slot], kLogoWidth, kLogoHeight};
    }
  }

  // Fetches AT MOST ONE missing logo and returns true while unattempted
  // slots remain. The radio must start the moment the network is up — the
  // owner measured the old all-at-once pre-playback fetch as a minute of
  // silence after every new build — so the main loop calls this between
  // audio passes, one bounded fetch at a time, only while the decode buffer
  // can bridge the pause. Each slot gets one attempt per boot; a failure
  // keeps its monogram until the next boot.
  bool fetchNextMissing(const StationSlots& slots) {
    if (!mounted_) return false;
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
      if ((attemptedMask_ & (1U << slot)) != 0U) continue;
      attemptedMask_ |= (1U << slot);
      const char* url = faviconFor(slots, slot);
      if (url == nullptr || url[0] == '\0') continue;
      if (logos_[slot].pixels != nullptr) continue;
      if (sidecarMatches(slot, url) && fileExists(slot)) continue;
      const char* reason = fetchOne(slot, url);
      Serial.printf("logo_fetch slot=%u result=%s reason=%s\n",
                    static_cast<unsigned>(slot),
                    reason == nullptr ? "ok" : "skip",
                    reason == nullptr ? "-" : reason);
      if (reason == nullptr) writeSidecar(slot, url);
      return slot + 1 < kSlots;
    }
    return false;
  }

  bool anyMissing(const StationSlots& slots) {
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
      const char* url = faviconFor(slots, slot);
      if (url == nullptr || url[0] == '\0') continue;
      if (logos_[slot].pixels != nullptr) continue;
      if (sidecarMatches(slot, url) && fileExists(slot)) continue;
      return true;
    }
    return false;
  }

  // A fresh pass may retry slots that failed this boot.
  void resetAttempts() { attemptedMask_ = 0; }

 private:
  static const char* faviconFor(const StationSlots& slots, std::size_t slot) {
    const std::int16_t directoryIndex = slots.directoryIndex(slot);
    if (directoryIndex == StationSlots::kBuiltIn) {
      return slot < open_radio::generated::kStationCount
                 ? open_radio::generated::kStations[slot].favicon
                 : nullptr;
    }
    if (static_cast<std::size_t>(directoryIndex) >=
        open_radio::generated::kDirectoryCount) {
      return nullptr;
    }
    return open_radio::generated::kDirectory[directoryIndex].favicon;
  }

  static std::uint32_t urlKey(const char* url) {
    std::uint32_t hash = 5381;
    for (const char* c = url; *c != '\0'; ++c) {
      hash = ((hash << 5) + hash) ^ static_cast<std::uint8_t>(*c);
    }
    return hash;
  }

  bool fileExists(std::size_t slot) {
    char path[16];
    std::snprintf(path, sizeof(path), "/l%u.bin", static_cast<unsigned>(slot));
    return SPIFFS.exists(path);
  }

  bool sidecarMatches(std::size_t slot, const char* url) {
    char path[16];
    std::snprintf(path, sizeof(path), "/l%u.url", static_cast<unsigned>(slot));
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) return false;
    std::uint32_t stored = 0;
    const std::size_t got =
        file.read(reinterpret_cast<std::uint8_t*>(&stored), sizeof(stored));
    file.close();
    // The sidecar pins the pixels to the URL that produced them, so a catalog
    // update that changes a station's icon refetches instead of showing the
    // previous broadcaster's mark forever.
    return got == sizeof(stored) && stored == urlKey(url);
  }

  void writeSidecar(std::size_t slot, const char* url) {
    char path[16];
    std::snprintf(path, sizeof(path), "/l%u.url", static_cast<unsigned>(slot));
    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file) return;
    const std::uint32_t key = urlKey(url);
    file.write(reinterpret_cast<const std::uint8_t*>(&key), sizeof(key));
    file.close();
  }

  // Returns nullptr when the logo landed, otherwise a static reason tag for
  // the serial log — the difference between a network layer that refuses
  // (begin/http/stream), a starved read (small) and a bad payload (dims/decode)
  // points at entirely different fixes.
  const char* fetchOne(std::size_t slot, const char* url) {
    std::uint8_t* download = downloadBuffer_;
    if (download == nullptr) return "alloc";
    std::size_t downloaded = 0;
    {
      // Declared BEFORE the HTTPClient that will use them: C++ destroys in
      // reverse order and HTTPClient's destructor calls _client->stop() on the
      // client it was begun with; with `request` declared first that call
      // landed on a client already destroyed (review of 2026-09-08).
      WiFiClientSecure secureClient;
      WiFiClient plainClient;
      HTTPClient request;
      request.setConnectTimeout(kFetchConnectTimeoutMs);
      request.setTimeout(kFetchConnectTimeoutMs);
      // Broadcaster favicon URLs routinely answer 301/302 (www rewrites, CDN
      // fronts). Not following them made those icons fail on EVERY boot.
      request.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      // HTTP/1.0 has no chunked transfer coding, so the body arrives with a
      // Content-Length or close-delimited — both readable by the bounded loop
      // below. The library's own chunk reader used before (writeToStream) had
      // no deadline: a server that announced a chunk and then held the socket
      // open parked this task for ever, and with it the Bluetooth start that
      // waits for the fetch pass (review of 2026-09-08). The CDN that chunked
      // under HTTP/1.1 (Radio Wnet, fixed 2026-08-02) answers HTTP/1.0 plain.
      request.useHTTP10(true);
      bool begun = false;
      if (std::strncmp(url, "https://", 8) == 0) {
        // The icon is decoration, not a trust boundary: it is drawn, never
        // executed, and the decoder rejects anything that is not a sane PNG.
        // Pinning certificates for eighty broadcaster CDNs is unmaintainable,
        // so transport integrity is deliberately waived for this one fetch.
        secureClient.setInsecure();
        // The mbedTLS handshake has its OWN clock — HTTPClient's connect and
        // stream timeouts do not bound it (arduino-esp32 #5928). Bounded so a
        // dead host releases the task, generous because nothing stalls on it.
        secureClient.setHandshakeTimeout(5);
        begun = request.begin(secureClient, url);
      } else {
        begun = request.begin(plainClient, url);
      }
      if (!begun) {
        return "begin";
      }
      const int status = request.GET();
      if (status != HTTP_CODE_OK) {
        request.end();
        // Negative codes are HTTPClient transport errors (connect, TLS,
        // read); positive ones came from the server.
        static char httpReason[16];
        std::snprintf(httpReason, sizeof(httpReason), "http%d", status);
        return httpReason;
      }
      // One bounded loop for both shapes of body. With Content-Length it stops
      // at the declared size; close-delimited (HTTP/1.0 without a length) it
      // drains what the socket buffered and stops when the peer has closed;
      // either way it stops at the deadline. Unknown length used to go through
      // writeToStream(), whose inner loop has no clock at all.
      const int declared = request.getSize();
      WiFiClient* stream = request.getStreamPtr();
      // nullptr once the peer has closed: HTTPClient asks connected(), and after
      // a FIN lwIP answers ENOTCONN. A 200 with an empty body (Content-Length: 0,
      // then close) reached stream->available() on a null pointer in 0.4.2 and
      // rebooted the radio on every boot while that icon stayed missing; 0.4.1
      // was guarded by request.connected() in the loop condition.
      if (stream == nullptr) {
        request.end();
        return "stream";
      }
      const std::uint32_t startedAt = millis();
      while (millis() - startedAt < kFetchBodyTimeoutMs) {
        const std::size_t available = stream->available();
        if (available == 0) {
          if (!request.connected()) break;
          delay(5);
          continue;
        }
        if (downloaded + available > kMaxDownloadBytes) {
          downloaded = 0;
          break;
        }
        downloaded += stream->readBytes(download + downloaded, available);
        if (declared >= 0 && downloaded >= static_cast<std::size_t>(declared)) break;
      }
      request.end();
    }
    if (downloaded < 64) {
      return "small";
    }
    const auto dims = parseImageDimensions(download, downloaded);
    if (!dims.valid || !dims.png || dims.width > 1600 || dims.height > 1600) {
      return "dims";
    }
    // The decoder skips unknown chunks by advancing an index it never bounds
    // against the buffer, so a chunk declaring more bytes than exist walks it
    // past the download allocation (review of 2026-09-08 — reachable through a
    // substituted favicon, since transport integrity is waived above). Every
    // chunk has to fit, and IEND has to be present, before pngle sees a byte.
    if (!pngChunksWellFormed(download, downloaded)) {
      return "chunks";
    }
    const bool stored = decodeAndStore(slot, download, downloaded, dims);
    return stored ? nullptr : "decode";
  }

  bool decodeAndStore(std::size_t slot, const std::uint8_t* data,
                      std::size_t length, const ImageDimensions& dims) {
    // Small sources decode at NATIVE size and are resampled by our own
    // integer cover loop — LovyanGFX's float upscale rejected a perfectly
    // healthy 57x57 PNG in the field (Meloradio, reason=decode) while every
    // downscale worked. Sources above the cap keep the library downscale.
    const bool nativeDecode = dims.width <= 256 && dims.height <= 256;
    const int spriteWidth = nativeDecode ? static_cast<int>(dims.width)
                                         : kLogoWidth;
    const int spriteHeight = nativeDecode ? static_cast<int>(dims.height)
                                          : kLogoHeight;
    LGFX_Sprite sprite(&M5.Display);
    sprite.setPsram(true);
    sprite.setColorDepth(16);
    if (sprite.createSprite(spriteWidth, spriteHeight) == nullptr) return false;
    sprite.fillSprite(TFT_BLACK);
    if (nativeDecode) {
      if (!sprite.drawPng(data, length, 0, 0)) {
        sprite.deleteSprite();
        return false;
      }
    } else {
      // Cover: scale by the LARGER ratio and let the overflow crop at the
      // sprite edge. Negative origin centres the crop; drawPng clips to the
      // sprite, so nothing is written out of bounds.
      float scale = static_cast<float>(kLogoWidth) / dims.width;
      const float vertical = static_cast<float>(kLogoHeight) / dims.height;
      if (vertical > scale) scale = vertical;
      const int drawnWidth = static_cast<int>(dims.width * scale);
      const int drawnHeight = static_cast<int>(dims.height * scale);
      const int offsetX = (kLogoWidth - drawnWidth) / 2;
      const int offsetY = (kLogoHeight - drawnHeight) / 2;
      if (!sprite.drawPng(data, length, offsetX, offsetY, kLogoWidth,
                          kLogoHeight, 0, 0, scale, scale)) {
        sprite.deleteSprite();
        return false;
      }
    }
    const std::size_t bytes =
        static_cast<std::size_t>(kLogoWidth) * kLogoHeight * 2;
    if (pixelStorage_[slot] == nullptr) {
      pixelStorage_[slot] = static_cast<std::uint16_t*>(
          heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (pixelStorage_[slot] == nullptr) {
      sprite.deleteSprite();
      return false;
    }
    // readPixel returns display-order RGB565 regardless of the sprite's
    // internal byte order — thousands of calls, once per icon, irrelevant
    // here and saves reasoning about LovyanGFX buffer endianness. The
    // native path runs the same integer cover resample the renderer uses.
    for (int y = 0; y < kLogoHeight; ++y) {
      for (int x = 0; x < kLogoWidth; ++x) {
        int sourceX = x;
        int sourceY = y;
        if (nativeDecode) {
          if (kLogoWidth * spriteHeight >= kLogoHeight * spriteWidth) {
            sourceX = (x * spriteWidth) / kLogoWidth;
            const int scaledH = (kLogoWidth * spriteHeight) / spriteWidth;
            sourceY = ((y + (scaledH - kLogoHeight) / 2) * spriteWidth) /
                      kLogoWidth;
          } else {
            sourceY = (y * spriteHeight) / kLogoHeight;
            const int scaledW = (kLogoHeight * spriteWidth) / spriteHeight;
            sourceX = ((x + (scaledW - kLogoWidth) / 2) * spriteHeight) /
                      kLogoHeight;
          }
          if (sourceX < 0) sourceX = 0;
          if (sourceY < 0) sourceY = 0;
          if (sourceX >= spriteWidth) sourceX = spriteWidth - 1;
          if (sourceY >= spriteHeight) sourceY = spriteHeight - 1;
        }
        pixelStorage_[slot][y * kLogoWidth + x] =
            sprite.readPixel(sourceX, sourceY);
      }
    }
    sprite.deleteSprite();
    char path[16];
    std::snprintf(path, sizeof(path), "/l%u.bin", static_cast<unsigned>(slot));
    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file) return false;
    const std::size_t written = file.write(
        reinterpret_cast<const std::uint8_t*>(pixelStorage_[slot]), bytes);
    file.close();
    if (written != bytes) return false;
    logos_[slot] = {pixelStorage_[slot], kLogoWidth, kLogoHeight};
    return true;
  }

  bool mounted_ = false;
  std::uint32_t attemptedMask_ = 0;
  std::array<open_radio::ui::RuntimeStationLogo, kSlots> logos_{};
  std::array<std::uint16_t*, kSlots> pixelStorage_{};
  std::uint8_t* downloadBuffer_ = nullptr;
};

}  // namespace open_radio::public_candidate
