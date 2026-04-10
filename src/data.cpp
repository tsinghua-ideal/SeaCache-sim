#include "data.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>

#include "headers.h"

int M;  // num rows in A
int N;  // num cols in A (and num rows in B)
int nzA;
int nzB;

// Let (r,c) be a valid non-zero entry in A. Then:
std::vector<int>
    *A  = nullptr,  // A[r]  = c
    *Ac = nullptr,  // Ac[c] = r
    *B  = nullptr,
    *Bc = nullptr;

std::vector<int> *sparchA = nullptr, *sparchAi = nullptr;

int *offsetarrayA = nullptr, *offsetarrayAc = nullptr;
int *offsetarrayB = nullptr, *offsetarrayBc = nullptr;

// Let row r be sampled from A, and it is the (r')th sampled row.
// Then for all indices [(r,c_1), (r,c_2), ..., (r,c_u)] in the row:
std::vector<int> *SA = nullptr;     //  SA[r']  = c
std::vector<int> *SAc = nullptr;    //  SAc[c]  = [hash1(r, ...) x u] (list of u copies of same hash)

// Let col c be sampled from B, and it is the (c')th sampled row.
// Then for all indices [(r_1,c), (r_2,c), ..., (r_v,c)] in the col:
std::vector<int> *SBc = nullptr;    // SBc[c']  = r
std::vector<int> *SB = nullptr;     // SB[r]    = [hash2(c, ...) x v] (list of v copies of same hash)
int *SAindex = nullptr, *SBcindex = nullptr;

extern double ha1, hb1;
extern double ha2, hb2;

void initialize_data_A() {
  try {
    if(A == nullptr) A = new std::vector<int>[I]();
    if(Ac == nullptr) Ac = new std::vector<int>[J]();
    if(SA == nullptr) SA = new std::vector<int>[I]();
    if(SAc == nullptr) SAc = new std::vector<int>[J]();
    if(offsetarrayA == nullptr) offsetarrayA = new int[I]();
    if(offsetarrayAc == nullptr) offsetarrayAc = new int[J]();
    if(SAindex == nullptr) SAindex = new int[I]();
  } catch (const std::bad_alloc &e) {
    std::cerr << "Error allocating memory for " << e.what() << std::endl;
    std::exit(1);
  }
}

void initialize_data_B() {
  try {
    if(B == nullptr) B = new std::vector<int>[I]();
    if(Bc == nullptr) Bc = new std::vector<int>[J]();
    if(SB == nullptr) SB = new std::vector<int>[I]();
    if(SBc == nullptr) SBc = new std::vector<int>[J]();
    if(offsetarrayB == nullptr) offsetarrayB = new int[J]();
    if(offsetarrayBc == nullptr) offsetarrayBc = new int[K]();
    if(SBcindex == nullptr) SBcindex = new int[K]();
  } catch (const std::bad_alloc &e) {
    std::cerr << "Error allocating memory for " << e.what() << std::endl;
    std::exit(1);
  }
}

void deinitialize_data() {
  if(A != nullptr) delete[] A;
  if(Ac != nullptr) delete[] Ac;
  if(SA != nullptr) delete[] SA;
  if(SAc != nullptr) delete[] SAc;
  if(offsetarrayA != nullptr) delete[] offsetarrayA;
  if(offsetarrayAc != nullptr) delete[] offsetarrayAc;

  if(B != nullptr) delete[] B;
  if(Bc != nullptr) delete[] Bc;
  if(SB != nullptr) delete[] SB;
  if(SBc != nullptr) delete[] SBc;
  if(offsetarrayB != nullptr) delete[] offsetarrayB;
  if(offsetarrayBc != nullptr) delete[] offsetarrayBc;
}

