#ifndef SIMULATOR_H
#define SIMULATOR_H

struct config {
    enum DataFlow   dataflow;
    enum InterOrder interorder;
    enum Format     format;

    Coord   I, // num rows in A
            J, // num cols in A, equiv. num rows in B
            K; // num cols in B
    Coord   tti,
            ttj,
            ttk;
    // block (tile?) size
    Coord   iii,
            jjj,
            kkk;
};

struct tile {
    // constants
    Coord major_dim, minor_dim;
        // - will equal pair of _tile_dim's
        // - assert(A.major_dim == B.minor_dim)
    const Coord **map, *offsets;// compressed sparse major -> minor map (e.g. CSR/CSC)

    // mutables
    const b16   *minor_wrap;
    const Coord *major_tile_idx, *minor_tile_idx;
    Coord       *begins, *sizes; // both indexed by major axis

};

struct cursor {
    // constants
    Coord   outer_dim, middle_dim, inner_dim;
    Coord   outer_tile_dim, middle_tile_dim, inner_tile_dim; // one of these axes is shared
    Coord   outer_ntiles, middle_ntiles, inner_ntiles;

    // mutables
    // tile indices
    b16     first;
    Coord   ti, // [0,... i_ntiles)
            tj, // [0,... j_ntiles)
            tk; // [0,... k_ntiles)
    b16     wrap_ti,
            wrap_tj,
            wrap_tk;
    Coord   *outer_tile_idx, *middle_tile_idx, *inner_tile_idx;
    b16     *outer_wrap, *middle_wrap, *inner_wrap; // temp; only used during advance_cursor
    struct tile A, B;

};

struct simulator_state {
    struct config cfg;
    struct cursor cursor;
};


extern struct Arena *global_persist;
extern struct Arena *global_temp;
extern struct simulator_state sim;

extern int prefetchSize;
extern bool adaptive_prefetch;

void configPartial(f32 partialA, f32 partialB, f32 partialC);
void reinitialize();
void runTile(int kkk);
void run();

extern int TI, TJ, TK;

struct simulator_state initialize_simulator(const struct config *cfg);

#endif
