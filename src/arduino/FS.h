// ESP32-style fs::FS / fs::File over RT-Thread DFS.
//
// MeshCore's file code falls back to the ESP32 API when no known platform is
// defined (open(path), open(path, "w", true), ...), so matching that API lets
// MeshCore's sources compile unmodified.
#pragma once

#include <memory>
#include "Stream.h"

namespace fs {

enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };

// As in the arduino-pico LittleFS API (used by MeshCore's companion DataStore).
struct FSInfo {
  size_t totalBytes = 0;
  size_t usedBytes = 0;
  size_t blockSize = 0;
  size_t pageSize = 0;
  size_t maxOpenFiles = 0;
  size_t maxPathLength = 0;
};

struct FileImpl;

class File : public Stream {
  std::shared_ptr<FileImpl> _impl;  // shared so copies (File is passed by value) close once

public:
  File() {}
  explicit File(std::shared_ptr<FileImpl> impl) : _impl(std::move(impl)) {}

  using Print::write;
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t len) override;

  int available() override;
  int read() override;
  int peek() override;
  size_t read(uint8_t* buf, size_t len);
  size_t readBytes(uint8_t* buf, size_t len) override { return read(buf, len); }
  using Stream::readBytes;
  void flush() override;

  bool seek(uint32_t pos, SeekMode mode = SeekSet);
  size_t position() const;
  size_t size() const;
  const char* name() const;      // base name, as in the ESP32/RP2040 Arduino cores
  const char* fullName() const;  // full path
  bool isDirectory() const;
  File openNextFile();           // directories: next entry (skips . and ..), or a false File
  void close();
  operator bool() const;
};

class FS {
public:
  // mode: "r", "w", "a", "r+", "w+", "a+". create=true also creates missing parent dirs.
  File open(const char* path, const char* mode = "r", bool create = false);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* from, const char* to);
  bool mkdir(const char* path);
  bool rmdir(const char* path);
  bool info(FSInfo& info);  // capacity of the volume holding "/"
};

// littlefs on the board's FS_REGION flash partition (4 MB), mounted at "/".
class InternalFlashFS : public FS {
public:
  bool begin(bool format_on_fail = true);
  bool format();  // erases everything, then remounts
  size_t totalBytes();
  size_t usedBytes();
};

}  // namespace fs

using fs::File;
using fs::FSInfo;
extern fs::InternalFlashFS InternalFS;
