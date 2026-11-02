#include "FS.h"

#include <rtthread.h>
#include <dfs_fs.h>
#include <dfs_posix.h>
#include <string.h>
#include "bf0_hal.h"
#include "drv_flash.h"

#define LOG_TAG "fs"
#include <ulog.h>

#define FS_DEVICE_NAME "fsroot"  // RT device name, max RT_NAME_MAX (8) chars
#define FS_MOUNT_POINT "/"
#define FS_TYPE        "lfs"
#define FS_MAX_PATH    64

fs::InternalFlashFS InternalFS;

namespace fs {

struct FileImpl {
  int fd = -1;
  DIR* dir = nullptr;  // set for directories
  size_t pos = 0;
  size_t size = 0;
  char path[FS_MAX_PATH];

  ~FileImpl() {
    if (fd >= 0) ::close(fd);
    if (dir) closedir(dir);
  }
};

// ---- File ----

size_t File::write(const uint8_t* buf, size_t len) {
  if (!_impl || len == 0) return 0;
  int n = ::write(_impl->fd, buf, len);
  if (n <= 0) return 0;
  _impl->pos += n;
  if (_impl->pos > _impl->size) _impl->size = _impl->pos;
  return n;
}

size_t File::read(uint8_t* buf, size_t len) {
  if (!_impl || len == 0) return 0;
  int n = ::read(_impl->fd, buf, len);
  if (n <= 0) return 0;
  _impl->pos += n;
  return n;
}

int File::read() {
  uint8_t c;
  return read(&c, 1) == 1 ? c : -1;
}

int File::peek() {
  if (!_impl) return -1;
  int c = read();
  if (c >= 0) seek(_impl->pos - 1);
  return c;
}

int File::available() {
  if (!_impl) return 0;
  return _impl->size > _impl->pos ? (int)(_impl->size - _impl->pos) : 0;
}

void File::flush() {
  if (_impl) fsync(_impl->fd);
}

bool File::seek(uint32_t pos, SeekMode mode) {
  if (!_impl) return false;
  off_t r = lseek(_impl->fd, (off_t)pos, (int)mode);
  if (r < 0) return false;
  _impl->pos = (size_t)r;
  return true;
}

size_t File::position() const { return _impl ? _impl->pos : 0; }
size_t File::size() const { return _impl ? _impl->size : 0; }
const char* File::fullName() const { return _impl ? _impl->path : ""; }

const char* File::name() const {
  if (!_impl) return "";
  const char* slash = strrchr(_impl->path, '/');
  return slash ? slash + 1 : _impl->path;
}

bool File::isDirectory() const { return _impl && _impl->dir; }

File File::openNextFile() {
  if (!_impl || !_impl->dir) return File();
  struct dirent* e;
  while ((e = readdir(_impl->dir)) != NULL) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    char child[FS_MAX_PATH];
    size_t n = strlen(_impl->path);
    bool root = n == 1 && _impl->path[0] == '/';
    if (snprintf(child, sizeof(child), "%s/%s", root ? "" : _impl->path, e->d_name) >= (int)sizeof(child))
      continue;
    return InternalFS.open(child, "r");
  }
  return File();
}
void File::close() { _impl.reset(); }
File::operator bool() const { return (bool)_impl; }

// ---- FS ----

static void make_parent_dirs(const char* path) {
  char tmp[FS_MAX_PATH];
  strncpy(tmp, path, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = 0;
  for (char* p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = 0;
      ::mkdir(tmp, 0);
      *p = '/';
    }
  }
}

File FS::open(const char* path, const char* mode, bool create) {
  if (!path || strlen(path) >= FS_MAX_PATH) return File();

  bool plus = strchr(mode, '+') != NULL;
  int flags;
  switch (mode[0]) {
    case 'r': flags = plus ? O_RDWR : O_RDONLY; break;
    case 'w': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
    case 'a': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
    default: return File();
  }
  if (create) make_parent_dirs(path);

  // Directories (read-only): iterate with openNextFile().
  struct stat st;
  if (mode[0] == 'r' && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
    DIR* d = opendir(path);
    if (!d) return File();
    auto impl = std::make_shared<FileImpl>();
    impl->dir = d;
    strcpy(impl->path, path);
    return File(impl);
  }

  int fd = ::open(path, flags, 0);
  if (fd < 0) return File();

  size_t size = (fstat(fd, &st) == 0) ? (size_t)st.st_size : 0;
  size_t pos = 0;
  if (mode[0] == 'a') {
    off_t end = lseek(fd, 0, SEEK_END);
    pos = end > 0 ? (size_t)end : 0;
  }

  auto impl = std::make_shared<FileImpl>();
  impl->fd = fd;
  impl->pos = pos;
  impl->size = size;
  strcpy(impl->path, path);
  return File(impl);
}

bool FS::exists(const char* path) {
  struct stat st;
  return stat(path, &st) == 0;
}

bool FS::remove(const char* path) { return unlink(path) == 0; }
bool FS::rename(const char* from, const char* to) { return ::rename(from, to) == 0; }
bool FS::mkdir(const char* path) { return ::mkdir(path, 0) == 0; }
bool FS::rmdir(const char* path) { return ::rmdir(path) == 0; }

bool FS::info(FSInfo& info) {
  struct statfs s;
  if (statfs(FS_MOUNT_POINT, &s) != 0) return false;
  info.blockSize = s.f_bsize;
  info.pageSize = s.f_bsize;
  info.totalBytes = (size_t)s.f_bsize * s.f_blocks;
  info.usedBytes = (size_t)s.f_bsize * (s.f_blocks - s.f_bfree);
  info.maxOpenFiles = DFS_FD_MAX;
  info.maxPathLength = FS_MAX_PATH;
  return true;
}

// ---- InternalFlashFS ----

bool InternalFlashFS::begin(bool format_on_fail) {
  if (!rt_device_find(FS_DEVICE_NAME)) {
    register_mtd_device(FS_REGION_START_ADDR, FS_REGION_SIZE, (char*)FS_DEVICE_NAME);
  }
  if (dfs_mount(FS_DEVICE_NAME, FS_MOUNT_POINT, FS_TYPE, 0, 0) == 0) {
    LOG_I("mounted littlefs at %s (%d KB)", FS_MOUNT_POINT, FS_REGION_SIZE / 1024);
    return true;
  }
  if (!format_on_fail) {
    LOG_E("mount failed");
    return false;
  }
  LOG_W("mount failed, formatting");
  return format();
}

bool InternalFlashFS::format() {
  dfs_unmount(FS_MOUNT_POINT);  // ok to fail if not mounted
  if (dfs_mkfs(FS_TYPE, FS_DEVICE_NAME) != 0) {
    LOG_E("mkfs failed");
    return false;
  }
  if (dfs_mount(FS_DEVICE_NAME, FS_MOUNT_POINT, FS_TYPE, 0, 0) != 0) {
    LOG_E("mount after format failed");
    return false;
  }
  LOG_I("formatted and mounted littlefs at %s", FS_MOUNT_POINT);
  return true;
}

size_t InternalFlashFS::totalBytes() {
  struct statfs s;
  return statfs(FS_MOUNT_POINT, &s) == 0 ? (size_t)s.f_bsize * s.f_blocks : 0;
}

size_t InternalFlashFS::usedBytes() {
  struct statfs s;
  return statfs(FS_MOUNT_POINT, &s) == 0 ? (size_t)s.f_bsize * (s.f_blocks - s.f_bfree) : 0;
}

}  // namespace fs
