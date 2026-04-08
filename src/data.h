#ifndef DATA_H
#define DATA_H

#include <queue>
#include <set>
#include <unordered_set>
#include <vector>
#include "headers.h"

static const int MAXN = 3000000;

// sparse matrices A, B and their transposes Ac, Bc
extern Coord **A, **Ac;
extern Coord **B, **Bc;
extern std::vector<int> *sparchA;

// store the offsets for A, Ac, B, Bc
extern Coord *offsetarrayA, *offsetarrayAc;
extern Coord *offsetarrayB, *offsetarrayBc;

// sample matrix
// extern int SI, SK;
// extern std::vector<Coord> *SA, *SAc;
// extern std::vector<Coord> *SBc, *SB;

#endif // DATA_H
