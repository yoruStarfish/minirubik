#include <stdio.h>
#include <string.h>
#define MINIRUBIK_EMBEDDED
#define main solver_main
#include "../solver.c"
#undef main
#include <assert.h>

int main(void)
{
    const state_t solved = {{0,1,2,3,4,5,6}, {0}};
    struct { uint8_t before, path[MAX_DEPTH], after; } guarded = {0xa5, {0}, 0x5a};
    /* Root/leaf bounds: success at exactly the bound, no child beyond it. */
    assert(dfs_search(&solved, 0, 0, ROOT_MOVE, guarded.path) == 0);
    assert(dfs_search(&solved, MAX_DEPTH, MAX_DEPTH, ROOT_MOVE, guarded.path) == MAX_DEPTH);
    assert(dfs_search(&solved, MAX_DEPTH + 1, MAX_DEPTH, ROOT_MOVE, guarded.path) == -1);
    assert(dfs_search(&solved, 0, MAX_DEPTH + 1, ROOT_MOVE, guarded.path) == -1);
    assert(dfs_search(&solved, 0, 1, ROOT_MOVE + 1, guarded.path) == -1);
    for (uint8_t move = 0; move < MOVES; ++move) {
        state_t state = apply_move(solved, move), original = state;
        assert(dfs_search(&state, 0, 0, ROOT_MOVE, guarded.path) == -1);
        assert(dfs_search(&state, 0, 1, ROOT_MOVE, guarded.path) == 1);
        state_t replay = apply_move(state, guarded.path[0]);
        assert(is_solved(&replay));
        /* Entering a subtree at depth ten must return total depth eleven. */
        assert(dfs_search(&state, 10, 11, ROOT_MOVE, guarded.path) == 11);
        replay = apply_move(state, guarded.path[10]);
        assert(is_solved(&replay));
        assert(dfs_search(&state, 11, 11, ROOT_MOVE, guarded.path) == -1);
        assert(memcmp(&state, &original, sizeof state) == 0);
    }
    /* Repeat a hard query around a solved query: static cursors must reset. */
    state_t hard;
    assert(parse_state("21345671111111", &hard));
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        assert(ida_star(&hard, guarded.path) == 11);
        state_t replay = hard;
        for (unsigned i = 0; i < 11; ++i)
            replay = apply_move(replay, guarded.path[i]);
        assert(is_solved(&replay));
        assert(ida_star(&solved, guarded.path) == 0);
    }
    assert(guarded.before == 0xa5 && guarded.after == 0x5a);
    printf("Static DFS bounds, subtree depths, reset and path guards passed; stack=%u bytes\n",
           (unsigned)sizeof search_stack);
    return 0;
}
