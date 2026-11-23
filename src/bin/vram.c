#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../include/vram.h"

typedef void* nvmlDevice_t;

typedef struct {
  unsigned long long total;
  unsigned long long free;
  unsigned long long used;
} nvmlMemory_t;

typedef struct {
  unsigned int version;
  unsigned long long total;
  unsigned long long reserved;
  unsigned long long free;
  unsigned long long used;
} nvmlMemoryV2_t;

typedef int (*nvmlInit_t)(void);
typedef int (*nvmlGetHandle_t)(unsigned int, nvmlDevice_t*);
typedef int (*nvmlGetMemory_t)(nvmlDevice_t, nvmlMemory_t*);
typedef int (*nvmlGetMemoryV2_t)(nvmlDevice_t, nvmlMemoryV2_t*);
typedef int (*nvmlGetTemperature_t)(nvmlDevice_t, int, unsigned int*);

typedef enum {
  NVML_UNKNOWN = 0,
  NVML_READY = 1,
  NVML_UNAVAILABLE = -1
} NvmlState;

#define DEVICE_PATH_SIZE 512
#define FILE_PATH_SIZE 1024
#define TEMPERATURE_CHANNEL_COUNT 21
#define SENSOR_NAME_SIZE 64

typedef struct {
  char usedPath[FILE_PATH_SIZE];
  char totalPath[FILE_PATH_SIZE];
  char temperaturePath[FILE_PATH_SIZE];
  bool hasVram;
  bool hasTemperature;
  bool scanned;
} SysfsGpu;

static NvmlState nvmlState = NVML_UNKNOWN;
static nvmlDevice_t nvmlDevice;
static nvmlGetMemory_t getMemory;
static nvmlGetMemoryV2_t getMemoryV2;
static nvmlGetTemperature_t getTemperature;
static SysfsGpu sysfsGpu;

static int copyPath(char* destination, size_t size, const char* source) {
  size_t length = strlen(source);
  if (length >= size) return -1;
  memcpy(destination, source, length + 1);
  return 0;
}

static int appendPath(char* destination, size_t size, const char* suffix) {
  size_t length = strlen(destination);
  size_t suffixLength = strlen(suffix);
  if (length + suffixLength >= size) return -1;
  memcpy(destination + length, suffix, suffixLength + 1);
  return 0;
}

static int buildPath(const char* directory, const char* suffix, char* out, size_t size) {
  if (copyPath(out, size, directory) != 0) return -1;
  return appendPath(out, size, suffix);
}

static int buildHwmonPath(const char* root, const char* directory, const char* suffix,
                          char* out, size_t size) {
  if (copyPath(out, size, root) != 0) return -1;
  if (appendPath(out, size, "/") != 0) return -1;
  if (appendPath(out, size, directory) != 0) return -1;
  return appendPath(out, size, suffix);
}

static ssize_t readFile(const char* path, char* buffer, size_t size) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1;

  ssize_t length = read(fd, buffer, size - 1);
  close(fd);
  if (length <= 0) return -1;

  buffer[length] = '\0';
  return length;
}

static int parseUnsigned(const char** cursor, unsigned long long* value) {
  const char* p = *cursor;

  while (*p == ' ' || *p == '\t') p++;
  if (*p < '0' || *p > '9') return -1;

  unsigned long long result = 0;
  while (*p >= '0' && *p <= '9') {
    result = result * 10 + (unsigned long long)(*p - '0');
    p++;
  }

  *value = result;
  *cursor = p;
  return 0;
}

static int readU64(const char* path, unsigned long long* value) {
  char buffer[32];
  if (readFile(path, buffer, sizeof(buffer)) < 0) return -1;

  const char* cursor = buffer;
  if (parseUnsigned(&cursor, value) != 0) return -1;
  if (*cursor != '\0' && *cursor != '\n' && *cursor != ' ' && *cursor != '\t') return -1;

  return 0;
}

static double readTemperature(const char* path) {
  char buffer[32];
  if (readFile(path, buffer, sizeof(buffer)) < 0) return -1.0;

  const char* cursor = buffer;
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') cursor++;

  bool negative = false;
  if (*cursor == '-') {
    negative = true;
    cursor++;
  } else if (*cursor == '+') {
    cursor++;
  }

  unsigned long long millidegrees;
  if (parseUnsigned(&cursor, &millidegrees) != 0) return -1.0;

  double celsius = (double)millidegrees / 1000.0;
  return negative ? -celsius : celsius;
}

static int findTemperaturePath(const char* devicePath, char* out, size_t outSize) {
  char hwmonRoot[DEVICE_PATH_SIZE];
  if (buildPath(devicePath, "/hwmon", hwmonRoot, sizeof(hwmonRoot)) != 0) return -1;

  DIR* directory = opendir(hwmonRoot);
  if (!directory) return -1;

  for (int channel = 1; channel <= TEMPERATURE_CHANNEL_COUNT; channel++) {
    char suffix[32];
    snprintf(suffix, sizeof(suffix), "/temp%d_input", channel);

    rewinddir(directory);

    struct dirent* entry;
    while ((entry = readdir(directory)) != NULL) {
      if (strncmp(entry->d_name, "hwmon", 5) != 0) continue;

      char path[FILE_PATH_SIZE];
      if (buildHwmonPath(hwmonRoot, entry->d_name, suffix, path, sizeof(path)) != 0) continue;
      if (readTemperature(path) < 0.0) continue;
      if (copyPath(out, outSize, path) != 0) continue;

      closedir(directory);
      return 0;
    }
  }

  closedir(directory);
  return -1;
}

static bool isDrmCardName(const char* name) {
  if (strncmp(name, "card", 4) != 0 || name[4] == '\0') return false;

  for (const char* c = name + 4; *c; c++) {
    if (*c < '0' || *c > '9') return false;
  }

  return true;
}

static int buildDevicePath(const char* cardName, char* out, size_t size) {
  if (copyPath(out, size, "/sys/class/drm/") != 0) return -1;
  if (appendPath(out, size, cardName) != 0) return -1;
  return appendPath(out, size, "/device");
}

static int findVramPaths(const char* devicePath, char* usedPath, size_t usedSize,
                         char* totalPath, size_t totalSize, unsigned long long* total) {
  if (buildPath(devicePath, "/mem_info_vram_used", usedPath, usedSize) != 0) return -1;
  if (buildPath(devicePath, "/mem_info_vram_total", totalPath, totalSize) != 0) return -1;

  unsigned long long used;
  if (readU64(usedPath, &used) != 0 || readU64(totalPath, total) != 0) {
    if (buildPath(devicePath, "/tile0/vram0/used_bytes", usedPath, usedSize) != 0) return -1;
    if (buildPath(devicePath, "/tile0/vram0/total_bytes", totalPath, totalSize) != 0) return -1;

    if (readU64(usedPath, &used) != 0 || readU64(totalPath, total) != 0) return -1;
  }

  if (*total == 0 || used > *total) return -1;

  return 0;
}

static void scanSysfsGpu(void) {
  if (sysfsGpu.scanned) return;

  sysfsGpu.scanned = true;
  sysfsGpu.hasVram = false;
  sysfsGpu.hasTemperature = false;

  DIR* directory = opendir("/sys/class/drm");
  if (!directory) {
    sysfsGpu.scanned = false;
    return;
  }

  unsigned long long bestTotal = 0;
  char bestDevice[DEVICE_PATH_SIZE] = "";
  char firstTemperature[FILE_PATH_SIZE] = "";

  struct dirent* entry;
  while ((entry = readdir(directory)) != NULL) {
    if (!isDrmCardName(entry->d_name)) continue;

    char devicePath[DEVICE_PATH_SIZE];
    if (buildDevicePath(entry->d_name, devicePath, sizeof(devicePath)) != 0) continue;

    if (firstTemperature[0] == '\0') {
      char temperaturePath[FILE_PATH_SIZE];
      if (findTemperaturePath(devicePath, temperaturePath, sizeof(temperaturePath)) == 0)
        copyPath(firstTemperature, sizeof(firstTemperature), temperaturePath);
    }

    char usedPath[FILE_PATH_SIZE];
    char totalPath[FILE_PATH_SIZE];
    unsigned long long total;
    if (findVramPaths(devicePath, usedPath, sizeof(usedPath),
                      totalPath, sizeof(totalPath), &total) != 0)
      continue;
    if (total <= bestTotal) continue;

    bestTotal = total;
    copyPath(bestDevice, sizeof(bestDevice), devicePath);
    copyPath(sysfsGpu.usedPath, sizeof(sysfsGpu.usedPath), usedPath);
    copyPath(sysfsGpu.totalPath, sizeof(sysfsGpu.totalPath), totalPath);
  }

  closedir(directory);

  if (bestTotal > 0) {
    sysfsGpu.hasVram = true;
    if (findTemperaturePath(bestDevice, sysfsGpu.temperaturePath,
                            sizeof(sysfsGpu.temperaturePath)) == 0)
      sysfsGpu.hasTemperature = true;
  } else if (firstTemperature[0] != '\0') {
    copyPath(sysfsGpu.temperaturePath, sizeof(sysfsGpu.temperaturePath), firstTemperature);
    sysfsGpu.hasTemperature = true;
  }

  if (!sysfsGpu.hasVram || !sysfsGpu.hasTemperature) sysfsGpu.scanned = false;
}

static NvmlState loadNvml(void) {
  if (nvmlState != NVML_UNKNOWN) return nvmlState;

  void* library = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
  if (!library) {
    nvmlState = NVML_UNAVAILABLE;
    return nvmlState;
  }

  nvmlInit_t initialize = (nvmlInit_t)dlsym(library, "nvmlInit_v2");
  if (!initialize) initialize = (nvmlInit_t)dlsym(library, "nvmlInit");

  nvmlGetHandle_t getHandle =
    (nvmlGetHandle_t)dlsym(library, "nvmlDeviceGetHandleByIndex_v2");
  if (!getHandle) getHandle = (nvmlGetHandle_t)dlsym(library, "nvmlDeviceGetHandleByIndex");

  getMemory = (nvmlGetMemory_t)dlsym(library, "nvmlDeviceGetMemoryInfo");
  getMemoryV2 = (nvmlGetMemoryV2_t)dlsym(library, "nvmlDeviceGetMemoryInfo_v2");
  getTemperature = (nvmlGetTemperature_t)dlsym(library, "nvmlDeviceGetTemperature");

  if (!initialize || !getHandle || !getMemory || !getTemperature) {
    nvmlState = NVML_UNAVAILABLE;
    return nvmlState;
  }

  if (initialize() != 0 || getHandle(0, &nvmlDevice) != 0) {
    nvmlState = NVML_UNAVAILABLE;
    return nvmlState;
  }

  nvmlState = NVML_READY;
  return nvmlState;
}

static double nvmlUsage(void) {
  if (getMemoryV2) {
    nvmlMemoryV2_t memory;
    memory.version = sizeof(nvmlMemoryV2_t) | (2u << 24);

    if (getMemoryV2(nvmlDevice, &memory) == 0 &&
        memory.total != 0 && memory.used <= memory.total)
      return 100.0 * (double)memory.used / (double)memory.total;
  }

  nvmlMemory_t memory;
  if (getMemory(nvmlDevice, &memory) != 0 ||
      memory.total == 0 || memory.used > memory.total)
    return -1.0;

  return 100.0 * (double)memory.used / (double)memory.total;
}

static double nvmlTemperature(void) {
  unsigned int temperature;
  if (getTemperature(nvmlDevice, 0, &temperature) != 0) return -1.0;

  return (double)temperature;
}

double getVramUsage() {
  if (nvmlState == NVML_READY || loadNvml() == NVML_READY) {
    double usage = nvmlUsage();
    if (usage >= 0.0) return usage;
  }

  scanSysfsGpu();
  if (!sysfsGpu.hasVram) return -1.0;

  unsigned long long used;
  unsigned long long total;
  if (readU64(sysfsGpu.usedPath, &used) != 0 ||
      readU64(sysfsGpu.totalPath, &total) != 0 ||
      total == 0 || used > total) {
    sysfsGpu.scanned = false;
    sysfsGpu.hasVram = false;
    return -1.0;
  }

  return 100.0 * (double)used / (double)total;
}

double getVramTemp() {
  if (nvmlState == NVML_READY || loadNvml() == NVML_READY) {
    double temperature = nvmlTemperature();
    if (temperature >= 0.0) return temperature;
  }

  scanSysfsGpu();
  if (!sysfsGpu.hasTemperature) return -1.0;

  double temperature = readTemperature(sysfsGpu.temperaturePath);
  if (temperature < 0.0) {
    sysfsGpu.scanned = false;
    sysfsGpu.hasTemperature = false;
  }

  return temperature;
}
