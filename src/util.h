#ifndef UTIL_H
#define UTIL_H

#include <random>
#include <vector>
#include "headers.h"

int getlog(int x);

// Global variables
static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_real_distribution<> dis(0.0, 1.0);

f64 hash1(int x, f64 a, f64 b, int pmod);
f64 hash2(int x, f64 a, f64 b, int pmod);

#endif // UTIL_H
