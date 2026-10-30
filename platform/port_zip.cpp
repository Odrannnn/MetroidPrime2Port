#include "port_zip.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace PortZip {
namespace {

namespace fs = std::filesystem;

fs::path FromUtf8(const std::string& text) {
  return fs::path(std::u8string(text.begin(), text.end()));
}

uint16_t Le16(const uint8_t* p) { return static_cast< uint16_t >(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p) {
  return static_cast< uint32_t >(p[0]) | (static_cast< uint32_t >(p[1]) << 8) |
         (static_cast< uint32_t >(p[2]) << 16) | (static_cast< uint32_t >(p[3]) << 24);
}
uint64_t Le64(const uint8_t* p) { return Le32(p) | (static_cast< uint64_t >(Le32(p + 4)) << 32); }

struct Entry {
  std::string name;
  uint16_t flags = 0;
  uint16_t method = 0;
  uint32_t crc = 0;
  uint64_t compressed = 0;
  uint64_t size = 0;
  uint64_t localOffset = 0;
};

bool ReadAt(std::ifstream& in, uint64_t offset, void* data, size_t length) {
  in.clear();
  in.seekg(static_cast< std::streamoff >(offset));
  in.read(static_cast< char* >(data), static_cast< std::streamsize >(length));
  return static_cast< size_t >(in.gcount()) == length;
}

bool ReadDirectory(std::ifstream& in, std::vector< Entry >& entries, std::string& error) {
  in.seekg(0, std::ios::end);
  const uint64_t fileSize = static_cast< uint64_t >(in.tellg());
  constexpr uint64_t kEocd = 22;
  if (fileSize < kEocd) {
    error = "not a zip file";
    return false;
  }
  const uint64_t window = std::min< uint64_t >(fileSize, kEocd + 0xFFFF);
  std::vector< uint8_t > tail(window);
  if (!ReadAt(in, fileSize - window, tail.data(), tail.size())) {
    error = "could not read the archive";
    return false;
  }
  size_t eocd = SIZE_MAX;
  for (size_t i = tail.size() - kEocd + 1; i-- > 0;) {
    if (Le32(&tail[i]) == 0x06054b50) {
      eocd = i;
      break;
    }
  }
  if (eocd == SIZE_MAX) {
    error = "not a zip file";
    return false;
  }
  uint64_t total = Le16(&tail[eocd + 10]);
  uint64_t dirSize = Le32(&tail[eocd + 12]);
  uint64_t dirOffset = Le32(&tail[eocd + 16]);
  const uint64_t eocdPos = fileSize - window + eocd;
  if (total == 0xFFFF || dirSize == 0xFFFFFFFFu || dirOffset == 0xFFFFFFFFu) {
    uint8_t locator[20];
    const bool hasLocator = eocdPos >= sizeof(locator) &&
                            ReadAt(in, eocdPos - sizeof(locator), locator, sizeof(locator)) &&
                            Le32(locator) == 0x07064b50;
    // A plain archive of exactly 65535 entries saturates the count without ZIP64.
    if (!hasLocator && (dirSize == 0xFFFFFFFFu || dirOffset == 0xFFFFFFFFu)) {
      error = "missing ZIP64 end of central directory";
      return false;
    }
    if (hasLocator) {
      uint8_t rec[56];
      if (!ReadAt(in, Le64(locator + 8), rec, sizeof(rec)) || Le32(rec) != 0x06064b50) {
        error = "bad ZIP64 end of central directory";
        return false;
      }
      total = Le64(rec + 32);
      dirSize = Le64(rec + 40);
      dirOffset = Le64(rec + 48);
    }
  }
  if (dirOffset > fileSize || dirSize > fileSize - dirOffset || dirSize > (1ull << 31)) {
    error = "bad central directory";
    return false;
  }
  std::vector< uint8_t > dir(dirSize);
  if (!ReadAt(in, dirOffset, dir.data(), dir.size())) {
    error = "could not read the central directory";
    return false;
  }
  size_t pos = 0;
  while (pos + 46 <= dir.size() && Le32(&dir[pos]) == 0x02014b50) {
    const uint8_t* h = &dir[pos];
    Entry e;
    e.flags = Le16(h + 8);
    e.method = Le16(h + 10);
    e.crc = Le32(h + 16);
    e.compressed = Le32(h + 20);
    e.size = Le32(h + 24);
    const size_t nameLen = Le16(h + 28);
    const size_t extraLen = Le16(h + 30);
    const size_t commentLen = Le16(h + 32);
    e.localOffset = Le32(h + 42);
    if (pos + 46 + nameLen + extraLen + commentLen > dir.size()) {
      error = "bad central directory entry";
      return false;
    }
    e.name.assign(reinterpret_cast< const char* >(h + 46), nameLen);
    const uint8_t* extra = h + 46 + nameLen;
    for (size_t x = 0; x + 4 <= extraLen;) {
      const uint16_t id = Le16(extra + x);
      const size_t len = Le16(extra + x + 2);
      if (x + 4 + len > extraLen) {
        break;
      }
      if (id == 0x0001) {
        // Only the fields whose 32-bit value is saturated are present, in this order.
        const uint8_t* f = extra + x + 4;
        size_t left = len;
        const auto take = [&](uint64_t& value, bool wanted) {
          if (wanted && left >= 8) {
            value = Le64(f);
            f += 8;
            left -= 8;
          }
        };
        take(e.size, e.size == 0xFFFFFFFFu);
        take(e.compressed, e.compressed == 0xFFFFFFFFu);
        take(e.localOffset, e.localOffset == 0xFFFFFFFFu);
      }
      x += 4 + len;
    }
    entries.push_back(std::move(e));
    pos += 46 + nameLen + extraLen + commentLen;
  }
  (void)total;
  return true;
}

bool ExtractEntry(std::ifstream& in, const Entry& e, const fs::path& out, std::string& error) {
  uint8_t local[30];
  if (!ReadAt(in, e.localOffset, local, sizeof(local)) || Le32(local) != 0x04034b50) {
    error = e.name + ": bad local header";
    return false;
  }
  uint64_t offset = e.localOffset + 30 + Le16(local + 26) + Le16(local + 28);
  std::error_code ec;
  fs::create_directories(out.parent_path(), ec);
  std::ofstream file(out, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = e.name + ": could not create the file";
    return false;
  }
  in.clear();
  in.seekg(static_cast< std::streamoff >(offset));

  constexpr size_t kChunk = 1 << 16;
  std::vector< uint8_t > inBuf(kChunk), outBuf(kChunk);
  uint32_t crc = static_cast< uint32_t >(crc32(0, nullptr, 0));
  uint64_t written = 0;
  uint64_t remaining = e.compressed;
  const auto emit = [&](const uint8_t* data, size_t length) {
    crc = static_cast< uint32_t >(crc32(crc, data, static_cast< uInt >(length)));
    written += length;
    file.write(reinterpret_cast< const char* >(data), static_cast< std::streamsize >(length));
    return static_cast< bool >(file);
  };
  z_stream zs{};
  const bool deflated = e.method == 8;
  if (deflated && inflateInit2(&zs, -15) != Z_OK) {
    error = e.name + ": zlib init failed";
    return false;
  }
  bool ok = true;
  bool streamEnd = !deflated;
  while (ok && remaining > 0) {
    const size_t want = static_cast< size_t >(std::min< uint64_t >(remaining, kChunk));
    in.read(reinterpret_cast< char* >(inBuf.data()), static_cast< std::streamsize >(want));
    if (static_cast< size_t >(in.gcount()) != want) {
      error = e.name + ": truncated archive";
      ok = false;
      break;
    }
    remaining -= want;
    if (!deflated) {
      ok = emit(inBuf.data(), want);
      if (!ok) {
        error = e.name + ": write failed";
      }
      continue;
    }
    zs.next_in = inBuf.data();
    zs.avail_in = static_cast< uInt >(want);
    while (ok && zs.avail_in > 0 && !streamEnd) {
      zs.next_out = outBuf.data();
      zs.avail_out = static_cast< uInt >(outBuf.size());
      const int rc = inflate(&zs, Z_NO_FLUSH);
      if (rc != Z_OK && rc != Z_STREAM_END) {
        error = e.name + ": corrupt compressed data";
        ok = false;
        break;
      }
      if (!emit(outBuf.data(), outBuf.size() - zs.avail_out)) {
        error = e.name + ": write failed";
        ok = false;
      }
      streamEnd = rc == Z_STREAM_END;
    }
  }
  if (deflated) {
    inflateEnd(&zs);
    if (ok && !streamEnd) {
      error = e.name + ": compressed data ends early";
      ok = false;
    }
  }
  file.close();
  if (ok && !file) {
    error = e.name + ": write failed";
    ok = false;
  }
  if (ok && written != e.size) {
    error = e.name + ": size mismatch";
    ok = false;
  }
  if (ok && crc != e.crc) {
    error = e.name + ": CRC mismatch";
    ok = false;
  }
  if (!ok) {
    fs::remove(out, ec);
  }
  return ok;
}

} // namespace

bool TexturePackFilter(std::string& name) {
  std::replace(name.begin(), name.end(), '\\', '/');
  if (name.empty() || name.back() == '/' || name.front() == '/') {
    return false;
  }
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast< char >(std::tolower(c)); });
  const auto ends = [&](const char* suffix) {
    const size_t n = std::strlen(suffix);
    return lower.size() >= n && lower.compare(lower.size() - n, n, suffix) == 0;
  };
  if (!ends(".png") && !ends(".dds")) {
    return false;
  }
  size_t start = 0;
  while (start <= name.size()) {
    size_t end = name.find('/', start);
    if (end == std::string::npos) {
      end = name.size();
    }
    const std::string part = name.substr(start, end - start);
    // ':' is a drive letter in the first part and an NTFS alternate stream in any other.
    if (part.empty() || part == "." || part == ".." || part == "__MACOSX" ||
        part.find(':') != std::string::npos) {
      return false;
    }
    if (end == name.size() && part.compare(0, 2, "._") == 0) {
      return false;
    }
    start = end + 1;
  }
  return true;
}

bool Extract(const std::string& zipPath, const std::string& destDir, const EntryFilter& filter,
             const std::function<void(size_t)>& progress, size_t& count, std::string& error) {
  count = 0;
  std::ifstream in(FromUtf8(zipPath), std::ios::binary);
  if (!in) {
    error = "could not open the file";
    return false;
  }
  std::vector< Entry > entries;
  if (!ReadDirectory(in, entries, error)) {
    return false;
  }
  const fs::path root = FromUtf8(destDir);
  for (const Entry& e : entries) {
    std::string rel = e.name;
    if (!filter(rel)) {
      continue;
    }
    if (e.flags & 1) {
      error = e.name + ": encrypted entries are not supported";
      return false;
    }
    if (e.method != 0 && e.method != 8) {
      error = e.name + ": unsupported compression method " + std::to_string(e.method);
      return false;
    }
    if (!ExtractEntry(in, e, root / FromUtf8(rel), error)) {
      return false;
    }
    ++count;
    if (progress) {
      progress(count);
    }
  }
  return true;
}

} // namespace PortZip
