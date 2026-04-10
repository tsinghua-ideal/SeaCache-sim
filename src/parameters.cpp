#include "parameters.h"

// smaple parameter p
double samplep = 0.01; // sampling probability
// sample parameter k
double samplek = 100;

int I; // num rows in A
int J; // num rows in A, equiv. num cols in B
int K; // num cols in B

int tti = 1, ttk = 1, ttj = 1;
// block size
int iii = 1000, jjj = 1000, kkk = 1000;

int BSRsize = 32;

InterOrder interorder;
DataFlow dataflow;
Format format;

string printInterOrder[] = {"IJK", "IKJ", "JKI", "JIK", "KIJ", "KJI"};
string printDataFlow[] = {"Inner", "Outer", "Gust"};
string printFormat[] = {"RR", "RC", "CR", "CC", "BB"};

int Asize;
int Bsize;
int Csize;
