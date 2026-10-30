// PortZip: stored, deflated and ZIP64 entries, the texture-pack filter, and
// CRC / unsupported-entry failures, on archives built here.
#include "port_test.h"
#include "port_zip.h"

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Item {
  std::string name;
  std::string data;
  bool deflate = false;
  bool zip64 = false;
  bool badCrc = false;
  uint16_t flags = 0;
};

void Put16(std::string& s, uint16_t v) {
  s.push_back(static_cast< char >(v & 0xFF));
  s.push_back(static_cast< char >(v >> 8));
}
void Put32(std::string& s, uint32_t v) {
  Put16(s, static_cast< uint16_t >(v & 0xFFFF));
  Put16(s, static_cast< uint16_t >(v >> 16));
}
void Put64(std::string& s, uint64_t v) {
  Put32(s, static_cast< uint32_t >(v));
  Put32(s, static_cast< uint32_t >(v >> 32));
}

std::string Deflate(const std::string& in) {
  z_stream zs{};
  deflateInit2(&zs, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
  std::string out(deflateBound(&zs, static_cast< uLong >(in.size())), '\0');
  zs.next_in = reinterpret_cast< Bytef* >(const_cast< char* >(in.data()));
  zs.avail_in = static_cast< uInt >(in.size());
  zs.next_out = reinterpret_cast< Bytef* >(out.data());
  zs.avail_out = static_cast< uInt >(out.size());
  deflate(&zs, Z_FINISH);
  out.resize(zs.total_out);
  deflateEnd(&zs);
  return out;
}

std::string BuildZip(const std::vector< Item >& items, bool zip64Tail, const std::string& comment = "") {
  std::string out, dir;
  for (const Item& item : items) {
    const uint32_t crc =
        static_cast< uint32_t >(crc32(crc32(0, nullptr, 0), reinterpret_cast< const Bytef* >(item.data.data()),
                                      static_cast< uInt >(item.data.size()))) ^
        (item.badCrc ? 1u : 0u);
    const std::string body = item.deflate ? Deflate(item.data) : item.data;
    const uint16_t method = item.deflate ? 8 : 0;
    const uint64_t offset = out.size();
    // Local header (sizes in the central directory only matter to the reader).
    Put32(out, 0x04034b50);
    Put16(out, 20);
    Put16(out, item.flags);
    Put16(out, method);
    Put32(out, 0);
    Put32(out, crc);
    Put32(out, static_cast< uint32_t >(body.size()));
    Put32(out, static_cast< uint32_t >(item.data.size()));
    Put16(out, static_cast< uint16_t >(item.name.size()));
    Put16(out, 4); // an extra field the reader must skip by its own length
    out += item.name;
    Put16(out, 0xCAFE);
    Put16(out, 0);
    out += body;
    Put32(dir, 0x02014b50);
    Put16(dir, 45);
    Put16(dir, 45);
    Put16(dir, item.flags);
    Put16(dir, method);
    Put32(dir, 0);
    Put32(dir, crc);
    const uint16_t extraLen = item.zip64 ? 28 : 0;
    Put32(dir, item.zip64 ? 0xFFFFFFFFu : static_cast< uint32_t >(body.size()));
    Put32(dir, item.zip64 ? 0xFFFFFFFFu : static_cast< uint32_t >(item.data.size()));
    Put16(dir, static_cast< uint16_t >(item.name.size()));
    Put16(dir, extraLen);
    Put16(dir, 0);
    Put16(dir, 0);
    Put16(dir, 0);
    Put32(dir, 0);
    Put32(dir, item.zip64 ? 0xFFFFFFFFu : static_cast< uint32_t >(offset));
    dir += item.name;
    if (item.zip64) {
      Put16(dir, 0x0001);
      Put16(dir, 24);
      Put64(dir, item.data.size());
      Put64(dir, body.size());
      Put64(dir, offset);
    }
  }
  const uint64_t dirOffset = out.size();
  out += dir;
  if (zip64Tail) {
    const uint64_t recPos = out.size();
    Put32(out, 0x06064b50);
    Put64(out, 44);
    Put16(out, 45);
    Put16(out, 45);
    Put32(out, 0);
    Put32(out, 0);
    Put64(out, items.size());
    Put64(out, items.size());
    Put64(out, dir.size());
    Put64(out, dirOffset);
    Put32(out, 0x07064b50);
    Put32(out, 0);
    Put64(out, recPos);
    Put32(out, 1);
  }
  Put32(out, 0x06054b50);
  Put16(out, 0);
  Put16(out, 0);
  Put16(out, zip64Tail ? 0xFFFF : static_cast< uint16_t >(items.size()));
  Put16(out, zip64Tail ? 0xFFFF : static_cast< uint16_t >(items.size()));
  Put32(out, static_cast< uint32_t >(dir.size()));
  Put32(out, zip64Tail ? 0xFFFFFFFFu : static_cast< uint32_t >(dirOffset));
  Put16(out, static_cast< uint16_t >(comment.size()));
  out += comment;
  return out;
}

void Write(const fs::path& path, const std::string& data) {
  std::ofstream(path, std::ios::binary) << data;
}

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator< char >(in), {});
}

std::string Payload(size_t size) {
  std::string s(size, '\0');
  uint32_t x = 12345;
  for (size_t i = 0; i < size; ++i) {
    x = x * 1664525u + 1013904223u;
    s[i] = static_cast< char >((i % 7 == 0) ? (x >> 24) : 'a' + (i % 5));
  }
  return s;
}

} // namespace

int main(int argc, char** argv) {
  // Real-archive mode: port_zip_tests <zip> <out dir> extracts every .png/.dds and prints the count.
  if (argc == 3) {
    size_t count = 0;
    std::string error;
    const bool ok = PortZip::Extract(argv[1], argv[2], PortZip::TexturePackFilter, nullptr, count, error);
    std::printf("%d %zu %s\n", ok ? 1 : 0, count, error.c_str());
    return ok ? 0 : 1;
  }

  const fs::path base = fs::temp_directory_path() / "port_zip_test";
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base);

  const std::string big = Payload(300000);
  const std::string small = Payload(100);
  const std::string wide = Payload(5000);
  std::vector< Item > items = {
      {"a.png", small, false},
      {"dir\\sub/b.DDS", big, true},
      {"z64/c.png", wide, true, true},
      {"../evil.png", small, false},
      {"/abs.png", small, false},
      {"C:/drive.png", small, false},
      {"__MACOSX/._x.png", small, false},
      {"dir/._fork.png", small, false},
      {"a/./b.png", small, false},
      {"notes.txt", small, false},
      {"dir/", "", false},
      {"empty.png", "", false},
  };
  size_t progressCalls = 0;
  const auto onProgress = [&](size_t) { ++progressCalls; };

  for (const bool tail64 : {false, true}) {
    const fs::path zip = base / (tail64 ? "z64.zip" : "plain.zip");
    const fs::path out = base / (tail64 ? "out64" : "out");
    Write(zip, BuildZip(items, tail64, tail64 ? std::string(40000, 'c') : std::string("hi")));
    size_t count = 0;
    std::string error;
    progressCalls = 0;
    REQUIRE_MSG(PortZip::Extract(zip.string(), out.string(), PortZip::TexturePackFilter, onProgress, count, error), error);
    CHECK_EQ(count, size_t(4));
    CHECK_EQ(progressCalls, size_t(4));
    CHECK(Read(out / "a.png") == small);
    CHECK(Read(out / "dir" / "sub" / "b.DDS") == big);
    CHECK(Read(out / "z64" / "c.png") == wide);
    CHECK(fs::exists(out / "empty.png") && fs::file_size(out / "empty.png") == 0);
    CHECK(!fs::exists(base / "evil.png"));
    CHECK(!fs::exists(out / "abs.png"));
    CHECK(!fs::exists(out / "notes.txt"));
    CHECK(!fs::exists(out / "__MACOSX"));
    CHECK(!fs::exists(out / "dir" / "._fork.png"));
    CHECK(!fs::exists(out / "a" / "b.png"));
    size_t files = 0;
    for (const auto& e : fs::recursive_directory_iterator(out)) {
      files += e.is_regular_file();
    }
    CHECK_EQ(files, size_t(4));
  }

  // A corrupt CRC fails the extraction and leaves no partial file.
  {
    const fs::path zip = base / "badcrc.zip";
    Write(zip, BuildZip({{"x.png", big, true, false, true}}, false));
    size_t count = 0;
    std::string error;
    CHECK(!PortZip::Extract(zip.string(), (base / "outbad").string(), PortZip::TexturePackFilter, nullptr, count, error));
    CHECK(error.find("CRC") != std::string::npos);
    CHECK(!fs::exists(base / "outbad" / "x.png"));
  }
  // Encrypted entries are refused with a clear error.
  {
    const fs::path zip = base / "enc.zip";
    Write(zip, BuildZip({{"x.png", small, false, false, false, 1}}, false));
    size_t count = 0;
    std::string error;
    CHECK(!PortZip::Extract(zip.string(), (base / "outenc").string(), PortZip::TexturePackFilter, nullptr, count, error));
    CHECK(error.find("encrypted") != std::string::npos);
  }
  // Not a zip.
  {
    const fs::path zip = base / "junk.zip";
    Write(zip, std::string(500, 'x'));
    size_t count = 0;
    std::string error;
    CHECK(!PortZip::Extract(zip.string(), (base / "outjunk").string(), PortZip::TexturePackFilter, nullptr, count, error));
    CHECK(!error.empty());
  }
  fs::remove_all(base, ec);
  return PORT_TEST_RESULT();
}
