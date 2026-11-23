#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "../../include/ram.h"

enum { MEMINFO_BUFFER_SIZE = 8192 };

static int findMemInfoValue(const char* buffer, const char* key,
                            unsigned long long* result) {
  const char* match = buffer;

  for (;;) {
    match = strstr(match, key);
    if (match == NULL) return -1;
    if (match == buffer || match[-1] == '\n') break;
    match++;
  }

  const char* cursor = match + strlen(key);
  while (*cursor == ' ' || *cursor == '\t') cursor++;
  if (*cursor < '0' || *cursor > '9') return -1;

  unsigned long long value = 0;
  while (*cursor >= '0' && *cursor <= '9') {
    value = value * 10 + (unsigned long long)(*cursor - '0');
    cursor++;
  }

  if (*cursor != ' ' && *cursor != '\t' && *cursor != '\n') return -1;

  *result = value;
  return 0;
}

double getRamUsage() {
  char meminfo[MEMINFO_BUFFER_SIZE];
  unsigned long long total = 0;
  unsigned long long available = 0;
  bool hasTotal = false;
  bool hasAvailable = false;
  size_t length = 0;

  int fd = open("/proc/meminfo", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1.0;

  for (;;) {
    if (length + 1 >= sizeof(meminfo)) {
      close(fd);
      return -1.0;
    }

    ssize_t bytes = read(fd, meminfo + length, sizeof(meminfo) - 1 - length);
    if (bytes < 0) {
      if (errno == EINTR) continue;
      close(fd);
      return -1.0;
    }
    if (bytes == 0) break;

    length += (size_t)bytes;
    meminfo[length] = '\0';

    if (!hasTotal && findMemInfoValue(meminfo, "MemTotal:", &total) == 0) hasTotal = true;
    if (!hasAvailable && findMemInfoValue(meminfo, "MemAvailable:", &available) == 0)
      hasAvailable = true;

    if (hasTotal && hasAvailable) break;
  }

  close(fd);

  if (!hasTotal || !hasAvailable || total == 0 || available > total) return -1.0;

  return 100.0 * (double)(total - available) / (double)total;
}
