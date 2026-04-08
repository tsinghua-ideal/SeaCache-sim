#include "util.h"
#include "headers.h"
#include <cmath>

// calculate log base 2 of x
int getlog(int x) {
    int ret = -1;
    while (x) {
        ret++;
        x >>= 1;
    }
    return ret;
}

// Hash function h1 and h2
f64 hash1(int x, f64 a, f64 b, int pmod) {
    // printf("%lf %lf\n", a*x+b, (f64)(std::fmod(a * x + b, pmod)));
    return (f64)(std::fmod(a * x + b, pmod)) / pmod;
}

f64 hash2(int x, f64 a, f64 b, int pmod) {
    return (f64)(std::fmod(a * x + b, pmod)) / pmod;
}
