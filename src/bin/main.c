#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "../../include/cpu.h"
#include "../../include/ram.h"
#include "../../include/storage.h"
#include "../../include/vram.h"

#define POLL_INTERVAL_NS 100000000L

static char outputBuffer[512];

static void printMetric(const char* name, double value) {
  if (value < 0.0) printf("%s: --\n", name);
  else printf("%s: %.2f%%\n", name, value);
}

static void printTemp(const char* name, double value) {
  if (value < 0.0) printf("%s: --\n", name);
  else printf("%s: %.1f°C\n", name, value);
}

static void waitForNextPoll(void) {
  struct timespec remaining = {0, POLL_INTERVAL_NS};

  while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
  }
}

int main() {
  setvbuf(stdout, outputBuffer, _IOFBF, sizeof(outputBuffer));

  cpu* cpuState = init();
  if (!cpuState) return 1;

  for (;;) {
    waitForNextPoll();

    printMetric("CPU", getCpuUsage(cpuState));
    printTemp("CPU temp", getCpuTemp());
    printMetric("RAM", getRamUsage());
    printMetric("Disk", getStorageUsage("/"));
    printMetric("VRAM", getVramUsage());
    printTemp("GPU temp", getVramTemp());

    fflush(stdout);
  }
}
