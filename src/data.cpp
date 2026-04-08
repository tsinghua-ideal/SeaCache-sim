#include "data.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "headers.h"


Coord **A   = NULL;  // A[r]  = [c_1, c_2, ..., c_u] are non-zero column indices in row r
Coord **Ac  = NULL; // Ac[c] = [r_1, r_2, ..., r_v] are non-zero row indices in column c
Coord **B   = NULL;
Coord **Bc  = NULL;

std::vector<int> *sparchA   = nullptr;

Coord *offsetarrayA     = nullptr;
Coord *offsetarrayAc    = nullptr;
Coord *offsetarrayB     = nullptr;
Coord *offsetarrayBc        = nullptr;

// Let row r be sampled from A, and it is the (r')th sampled row.
// Then for all indices [(r,c_1), (r,c_2), ..., (r,c_u)] in the row:
// std::vector<Coord> *SA = nullptr;  //  SA[r']  = [c_1, c_2, ..., c_u]
// std::vector<Coord> *SAc = nullptr; //  SAc[c]  = [hash1(r, ...)] x u (list of u copies of same hash)

// Let col c be sampled from B, and it is the (c')th sampled row.
// Then for all indices [(r_1,c), (r_2,c), ..., (r_v,c)] in the col:
// std::vector<Coord> *SBc = nullptr; // SBc[c']  = [r_1, r_2, ..., r_v]
// std::vector<Coord> *SB = nullptr;  // SB[r]    = [hash2(c, ...)] x v (list of v copies of same hash)

extern f64 ha1, hb1;
extern f64 ha2, hb2;
