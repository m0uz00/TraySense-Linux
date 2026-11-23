#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../include/cpu.h"

#define CPU_LINE_BUFFER_SIZE 512
#define SENSOR_PATH_SIZE 512
#define SENSOR_NAME_SIZE 64

enum {
  CPU_TIME_USER = 0,
  CPU_TIME_NICE,
  CPU_TIME_SYSTEM,
  CPU_TIME_IDLE,
  CPU_TIME_IOWAIT,
  CPU_TIME_IRQ,
  CPU_TIME_SOFTIRQ,
  CPU_TIME_STEAL,
  CPU_TIME_COUNT
};

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

static ssize_t readFirstLine(const char* path, char* buffer, size_t size) {
  ssize_t length = readFile(path, buffer, size);
  if (length < 0) return -1;

  char* newline = memchr(buffer, '\n', (size_t)length);
  if (newline) *newline = '\0';

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

static int readCpuTimes(unsigned long long* total, unsigned long long* busy) {
  char line[CPU_LINE_BUFFER_SIZE];

  if (readFirstLine("/proc/stat", line, sizeof(line)) < 0) return -1;
  if (strncmp(line, "cpu", 3) != 0) return -1;
  if (line[3] != ' ' && line[3] != '\t') return -1;

  const char* cursor = line + 3;
  unsigned long long times[CPU_TIME_COUNT];

  for (int i = 0; i < CPU_TIME_COUNT; i++) {
    if (parseUnsigned(&cursor, &times[i]) != 0) return -1;
  }

  unsigned long long sum = 0;
  for (int i = 0; i < CPU_TIME_COUNT; i++) sum += times[i];

  *total = sum;
  *busy = sum - times[CPU_TIME_IDLE] - times[CPU_TIME_IOWAIT];

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

static int findTemperatureSensor(const char* driverName, char* sensorPath,
                                 size_t sensorPathSize, double* temperature) {
  DIR* directory = opendir("/sys/class/hwmon");
  if (!directory) return -1;

  struct dirent* entry;
  char path[SENSOR_PATH_SIZE];
  char contents[SENSOR_NAME_SIZE];

  while ((entry = readdir(directory)) != NULL) {
    int length = snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", entry->d_name);
    if (length < 0 || (size_t)length >= sizeof(path)) continue;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) continue;

    ssize_t bytes = read(fd, contents, sizeof(contents) - 1);
    close(fd);
    if (bytes <= 0) continue;

    contents[bytes] = '\0';

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

cpu* init() {
  cpu* state = malloc(sizeof(cpu));
  if (!state) return NULL;

  if (readCpuTimes(&state->lastTotal, &state->lastBusy) != 0) {
    free(state);
    return NULL;
  }

  return state;
}

double getCpuUsage(cpu* state) {
  unsigned long long total;
  unsigned long long busy;

  if (readCpuTimes(&total, &busy) != 0) return -1.0;

  unsigned long long totalDelta = total - state->lastTotal;
  unsigned long long busyDelta = busy - state->lastBusy;

  state->lastTotal = total;
  state->lastBusy = busy;

  if (totalDelta == 0) return -1.0;

  return 100.0 * (double)busyDelta / (double)totalDelta;
}

double getCpuTemp() {
  static char cachedSensorPath[SENSOR_PATH_SIZE];
  static bool hasCachedSensor = false;

  if (hasCachedSensor) {
    double temperature = readTemperature(cachedSensorPath);
    if (temperature >= 0.0) return temperature;
    hasCachedSensor = false;
  }

  double temperature;

  if (findTemperatureSensor("k10temp", cachedSensorPath, sizeof(cachedSensorPath),
                            &temperature) == 0) {
    hasCachedSensor = true;
    return temperature;
  }

  if (findTemperatureSensor("coretemp", cachedSensorPath, sizeof(cachedSensorPath),
                            &temperature) == 0) {
    hasCachedSensor = true;
    return temperature;
  }

  return -1.0;
}
