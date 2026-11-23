#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../../include/cpu.h"
#include "../../include/ram.h"
#include "../../include/storage.h"
#include "../../include/vram.h"

static char outputBuffer[512];

static void printMetric(const char* name, double value) {
  if (value < 0.0) printf("%s: --\n", name);
  else printf("%s: %.2f%%\n", name, value);
}

static void printTemp(const char* name, double value) {
  if (value < 0.0) printf("%s: --\n", name);
  else printf("%s: %.1f°C\n", name, value);
}

int main() {
  setvbuf(stdout, outputBuffer, _IOFBF, sizeof(outputBuffer));

  cpu* cpuState = init();
  if (!cpuState) return 1;

  sleep(1);

  printMetric("CPU", getCpuUsage(cpuState));
  printTemp("CPU temp", getCpuTemp());
  printMetric("RAM", getRamUsage());
  printMetric("Disk", getStorageUsage("/"));
  printMetric("VRAM", getVramUsage());
  printTemp("GPU temp", getVramTemp());

  free(cpuState);

  return 0;
}
