#ifndef DATA_H
#define DATA_H

#include <queue>
#include <set>
#include <unordered_set>
#include <vector>

static const int MAXN = 3000000;

// input matrix size and number of non-zero elements
extern int N, M, nzA, nzB;

// sparse matrices A, B and their transposes Ac, Bc
extern std::vector<int> *A, *Ac;
extern std::vector<int> *sparchA, *sparchAi;
extern std::vector<int> *B, *Bc;

// store the offsets for A, Ac, B, Bc
extern int *offsetarrayA, *offsetarrayAc;
extern int *offsetarrayB, *offsetarrayBc;

// sample matrix
extern int SI, SK;
extern std::vector<int> *SA, *SAc, *SBc, *SB;
extern int *SAindex, *SBcindex;

// Read input matrices A and B from files
void readInputMatrices(const char *fileA, const char *fileB);

void initialize_data_A();
void initialize_data_B();
void deinitialize_data();

#endif // DATA_H
