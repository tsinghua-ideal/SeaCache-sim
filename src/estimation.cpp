#include "headers.h"
#include "arena.h"
#include "util.h"

long long estEffMAC;

struct pair_hash {
    template <typename T1, typename T2>
    std::size_t operator()(const std::pair<T1, T2> &p) const {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

void getParameter()
{
    struct Arena_Mark mark = arena_snap(global_temp);
    b8  *seen;
    Coord *dirty;
    seen = (b8 *)arena_push(global_temp, sim.cfg.K*sizeof(*seen), __alignof__(*seen), 1);
    dirty= (Coord*)arena_push(global_temp, sim.cfg.K*sizeof(*dirty), __alignof__(*dirty), 0);

    estEffMAC   = 0;
    u64 estnnzC = 0;

    for (Coord i = 0; i < sim.cfg.I; ++i) {
        Coord lenA = offsetarrayA[i+1] - offsetarrayA[i];
        Coord ndirty = 0;

        for (Coord j = 0; j < lenA; ++j) {
            Coord tmpx = A[i][j];
            Coord lenB = offsetarrayB[tmpx+1] - offsetarrayB[tmpx];
            estEffMAC += lenB;

            for (Coord k = 0; k < lenB; ++k) {
                Coord tmpk = B[tmpx][k];
                if (!seen[tmpk]) {
                    seen[tmpk]      = 1;
                    dirty[ndirty++] = tmpk;
                }
            }
        }

        estnnzC += ndirty;
        for (Coord d = 0; d < ndirty; d++)
            seen[dirty[d]] = 0;
    }

    printf("MAC: %lld   nnzC: %lld\n", estEffMAC, estnnzC);
    arena_rewind(mark);
}