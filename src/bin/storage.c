#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "../../include/storage.h"

#define SENSOR_PATH_SIZE 512
#define SENSOR_NAME_SIZE 64

static ssize_t readFile(const char* path, char* buffer, size_t size) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1;

  ssize_t length;
  do {
    length = read(fd, buffer, size - 1);
  } while (length < 0 && errno == EINTR);

  close(fd);
  if (length <= 0) return -1;

  buffer[length] = '\0';
  return length;
}

static double readTemperature(const char* path) {
  char buffer[32];
  if (readFile(path, buffer, sizeof(buffer)) < 0) return -1.0;

  double millidegrees;
  if (sscanf(buffer, "%lf", &millidegrees) != 1) return -1.0;

  return millidegrees / 1000.0;
}

static int findTemperatureSensor(const char* driverName, char* sensorPath,
                                 size_t sensorPathSize, double* temperature) {
  DIR* directory = opendir("/sys/class/hwmon");
  if (!directory) return -1;

  char path[SENSOR_PATH_SIZE];
  char contents[SENSOR_NAME_SIZE];

  struct dirent* entry;
  while ((entry = readdir(directory)) != NULL) {
    int length = snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", entry->d_name);
    if (length < 0 || (size_t)length >= sizeof(path)) continue;
    if (readFile(path, contents, sizeof(contents)) < 0) continue;

    char driver[SENSOR_NAME_SIZE];
    if (sscanf(contents, "%63s", driver) != 1) continue;
    if (strcmp(driver, driverName) != 0) continue;

    length = snprintf(path, sizeof(path), "/sys/class/hwmon/%s/temp1_input", entry->d_name);
    if (length < 0 || (size_t)length >= sizeof(path)) continue;

    double value = readTemperature(path);
    if (value < 0.0) continue;

    size_t pathLength = strlen(path);
    if (pathLength >= sensorPathSize) continue;

    memcpy(sensorPath, path, pathLength + 1);
    *temperature = value;

    closedir(directory);
    return 0;
  }

  closedir(directory);
  return -1;
}

double getStorageUsage(const char* path) {
  struct statvfs filesystem;

  if (statvfs(path, &filesystem) != 0) return -1.0;

  unsigned long long used = filesystem.f_blocks - filesystem.f_bfree;
  unsigned long long total = used + filesystem.f_bavail;

  if (total == 0) return -1.0;

  return 100.0 * (double)used / (double)total;
}

double getStorageTemp() {
  static char cachedSensorPath[SENSOR_PATH_SIZE];
  static bool hasCachedSensor = false;

  if (hasCachedSensor) {
    double temperature = readTemperature(cachedSensorPath);
    if (temperature >= 0.0) return temperature;
    hasCachedSensor = false;
  }

  double temperature;

  if (findTemperatureSensor("nvme", cachedSensorPath, sizeof(cachedSensorPath),
                            &temperature) == 0) {
    hasCachedSensor = true;
    return temperature;
  }

  if (findTemperatureSensor("drivetemp", cachedSensorPath, sizeof(cachedSensorPath),
                            &temperature) == 0) {
    hasCachedSensor = true;
    return temperature;
  }

  return -1.0;
}
