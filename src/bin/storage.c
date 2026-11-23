#include <sys/statvfs.h>

#include "../../include/storage.h"

double getStorageUsage(const char* path) {
  struct statvfs filesystem;

  if (statvfs(path, &filesystem) != 0) return -1.0;

  unsigned long long used = filesystem.f_blocks - filesystem.f_bfree;
  unsigned long long total = used + filesystem.f_bavail;

  if (total == 0) return -1.0;

  return 100.0 * (double)used / (double)total;
}
