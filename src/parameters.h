#ifndef PARAMETER_H
#define PARAMETER_H

#include <string.h>
#include <string>
using namespace std;

enum DataFlow {
    Inner, // 0
    Outer, // 1
    Gust   // 2
};

enum InterOrder {
    IJK, // 0
    IKJ, // 1
    JKI, // 2
    JIK, // 3
    KIJ, // 4
    KJI  // 5
};

enum Format {
    RR, // 0
    RC, // 1
    CR, // 2
    CC, // 3
    BB  // 4
};

extern string printInterOrder[10];
extern string printDataFlow[10];
extern string printFormat[10];

extern int blockformat;
extern int BSRsize;

const enum InterOrder interorder= IJK;
const enum DataFlow dataflow    = Gust;
const enum Format format        = RR;

extern int Asize;
extern int Bsize;
extern int Csize;

#endif