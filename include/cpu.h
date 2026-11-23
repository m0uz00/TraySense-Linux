#pragma once

#include <stdio.h>

typedef struct cpu {
  unsigned long long lastTotal;
  unsigned long long lastBusy;
} cpu;

cpu* init();

double getCpuUsage(cpu* state);

double getCpuTemp();
