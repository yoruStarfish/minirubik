/* Include the implementation to test internal indexing and abstract distances. */
#define main solver_main
#include "../solver.c"
#undef main
#include <assert.h>

static void check_edges(state_t state, bool orientation)
{
    const uint8_t *pdb = orientation ? pdb_orientation_packed : pdb_permutation_packed;
    uint32_t rank = orientation ? get_orientation_index(state.o) : get_permutation_index(state.p);
    uint8_t h = get_pdb_distance(pdb, rank);
    bool descending = h == 0;
    assert(h < 15 && ((h == 0) == (rank == 0)));
    for (uint8_t m = 0; m < MOVES; ++m) {
        state_t next = apply_move(state, m);
        uint32_t nr = orientation ? get_orientation_index(next.o) : get_permutation_index(next.p);
        uint8_t nh = get_pdb_distance(pdb, nr);
        assert(h <= nh + 1 && nh <= h + 1);
        if (nh + 1 == h) descending = true;
    }
    assert(descending);
}

int main(void)
{
    state_t state = {{0,1,2,3,4,5,6}, {0}};
    for (uint32_t r = 0; r < ORIENTATIONS; ++r) {
        uint32_t n = r, sum = 0;
        for (unsigned i = 0; i < 6; ++i) {
            state.o[i] = (uint8_t)(n % 3); n /= 3; sum += state.o[i];
        }
        state.o[6] = (uint8_t)((3 - sum % 3) % 3);
        assert(get_orientation_index(state.o) == r);
        check_edges(state, true);
    }
    memset(state.o, 0, sizeof state.o);
    for (uint32_t r = 0; r < PERMUTATIONS; ++r) {
        uint8_t available[7] = {0,1,2,3,4,5,6};
        uint32_t n = r;
        for (unsigned i = 0; i < 7; ++i) {
            unsigned q = n / factorial[i]; n %= factorial[i];
            state.p[i] = available[q];
            for (unsigned j = q; j + 1 < 7 - i; ++j) available[j] = available[j + 1];
        }
        assert(get_permutation_index(state.p) == r);
        check_edges(state, false);
    }
    puts("All 5769 projected indices and PDB shortest-distance conditions passed");
    return 0;
}
