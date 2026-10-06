#include <stdint.h>
#ifndef MINIRUBIK_EMBEDDED
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif
#include <stdbool.h>
#include "pdb_data.h"
#include "transition_data.h"

enum { CUBIES = 7, PERMUTATIONS = 5040, ORIENTATIONS = 729,
       MOVES = 9, MAX_DEPTH = 11, ROOT_MOVE = 9 };
typedef struct { uint8_t p[CUBIES], o[CUBIES]; } state_t;

#ifndef MINIRUBIK_EMBEDDED
static const char *const move_names[MOVES] =
    {"R", "R2", "R'", "B", "B2", "B'", "D", "D2", "D'"};
static const uint8_t inverse_move[MOVES] = {2, 1, 0, 5, 4, 3, 8, 7, 6};
#endif
#ifndef MINIRUBIK_EMBEDDED
/* Reference quarter-turn model is used only by host verification. */
static const uint8_t move_face[MOVES] = {0, 0, 0, 1, 1, 1, 2, 2, 2};
static const uint8_t move_turns[MOVES] = {1, 2, 3, 1, 2, 3, 1, 2, 3};
static const uint8_t source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6}, {0, 1, 2, 4, 5, 6, 3}, {0, 2, 5, 3, 1, 4, 6}
};
static const uint8_t twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0}, {0, 0, 0, 1, 2, 1, 2}, {0, 0, 0, 0, 0, 0, 0}
};
#endif

/* Full move maps, composed offline from source/twist quarter turns.
 * Each row maps destination -> original source; twist is modulo 3.
 * Composition: Snew[i]=S[source[f][i]],
 * Tnew[i]=(T[source[f][i]]+twist[f][i]) % 3. */
static const uint8_t move_source[MOVES][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6}, /* R */
    {4, 3, 2, 1, 0, 5, 6}, /* R2 */
    {3, 0, 2, 4, 1, 5, 6}, /* R' */
    {0, 1, 2, 4, 5, 6, 3}, /* B */
    {0, 1, 2, 5, 6, 3, 4}, /* B2 */
    {0, 1, 2, 6, 3, 4, 5}, /* B' */
    {0, 2, 5, 3, 1, 4, 6}, /* D */
    {0, 5, 4, 3, 2, 1, 6}, /* D2 */
    {0, 4, 1, 3, 5, 2, 6}, /* D' */
};
static const uint8_t move_twist[MOVES][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0}, /* R */
    {0, 0, 0, 0, 0, 0, 0}, /* R2 */
    {1, 2, 0, 2, 1, 0, 0}, /* R' */
    {0, 0, 0, 1, 2, 1, 2}, /* B */
    {0, 0, 0, 0, 0, 0, 0}, /* B2 */
    {0, 0, 0, 1, 2, 1, 2}, /* B' */
    {0, 0, 0, 0, 0, 0, 0}, /* D */
    {0, 0, 0, 0, 0, 0, 0}, /* D2 */
    {0, 0, 0, 0, 0, 0, 0}, /* D' */
};

/* First nine rows have six successors. The root has all nine.
 * Consecutive turns of one face combine into one move (or cancel). */
static const uint8_t valid_next_moves[10][9] = {
    /*
    R: 0-2
    B: 3-5
    D: 6-8
    */
    {3,4,5,6,7,8}, {3,4,5,6,7,8}, {3,4,5,6,7,8},
    {0,1,2,6,7,8}, {0,1,2,6,7,8}, {0,1,2,6,7,8},
    {0,1,2,3,4,5}, {0,1,2,3,4,5}, {0,1,2,3,4,5},
    {0,1,2,3,4,5,6,7,8} // root
};
static const uint8_t successor_count[10] = {6,6,6,6,6,6,6,6,6,9};
static const uint16_t powers_of_3[6] = {1,3,9,27,81,243};
static const uint16_t factorial[7] = {720,120,24,6,2,1,1};
static const uint8_t mod3[15] = {0,1,2,0,1,2,0,1,2,0,1,2,0,1,2};

static uint8_t get_pdb_distance(const uint8_t *pdb, uint32_t index)
{
    // 15U = 0x0F
    // index & 1U gives 0 for even index and 1 for odd index
    // (index & 1U) << 2 gives 0 for even index and 4 for odd index
    return (uint8_t)((pdb[index >> 1] >> ((index & 1U) << 2)) & 15U);
}

/* Least-significant trit first, matching the offline PDB generator.
 * Select 0, weight, or twice weight without multiplication. */
static uint32_t get_orientation_index(const uint8_t ori[CUBIES])
{
    /* index = summation_{i=0}^5 ori[i]*(3^i) */
    // 0U-0 = 0, 0U-1 = 0xFFFFFFFFF
    /* 
    If ori[i] == 0, then index += (weight & 0) | 0, that is 0 
    If ori[i] == 1, then index += (weight & 0xFFFFFFFF) | 0, that is weight 
    If ori[i] == 2, then index += (weight << 1) & 0xFFFFFFFF, that is twice the weight 
    */
    uint32_t index = 0;
    for (uint8_t i = 0; i < 6; ++i) {
        uint32_t weight = powers_of_3[i];
        index += (weight & (0U - (uint32_t)(ori[i] == 1))) |
                 ((weight << 1) & (0U - (uint32_t)(ori[i] == 2)));
    }
    return index; // Range: 0~728
}

/* Each smaller following cubie contributes one factorial weight.
 * This is Lehmer ranking using additions, with no mul/div/rem. */
static uint32_t get_permutation_index(const uint8_t perm[CUBIES])
{
    // There are 7! = 5040 permutations of 7 cubies. The index is the Lehmer code of the permutation.
    /*
    index =
    (count of smaller numbers to the right of position 0) × 6!
  + (count of smaller numbers to the right of position 1) × 5!
  + (count of smaller numbers to the right of position 2) × 4!
  + (count of smaller numbers to the right of position 3) × 3!
  + (count of smaller numbers to the right of position 4) × 2!
  + (count of smaller numbers to the right of position 5) × 1!
    */
   // for example, 0 is [0, 1, 2], 1 is [0, 2, 1], 2 is [1, 0, 2], 3 is [1, 2, 0], 4 is [2, 0, 1], 5 is [2, 1, 0]
    uint32_t index = 0;
    for (uint8_t i = 0; i < 6; ++i) // i means the position of the current cubie in the permutation
        for (uint8_t j = (uint8_t)(i + 1); j < CUBIES; ++j) // j means the position of the cubies after the current cubie
            index += factorial[i] & (0U - (uint32_t)(perm[j] < perm[i]));
    return index; // Range: 0~5039
    /*
    uint32_t index = 0;

    for (uint8_t i = 0; i < 6; ++i) {
        for (uint8_t j = i + 1; j < CUBIES; ++j) {
            if (perm[j] < perm[i]) {
                index += factorial[i];
            }
        }
    }

    return index;
    */
}

static inline uint8_t coordinate_heuristic(uint16_t p, uint16_t o)
{
    uint8_t ori = get_pdb_distance(pdb_orientation_packed, o);
    uint8_t perm = get_pdb_distance(pdb_permutation_packed, p);
    return ori > perm ? ori : perm;
}

static uint8_t evaluate_heuristic(const state_t *state)
{
    return coordinate_heuristic((uint16_t)get_permutation_index(state->p),
                                (uint16_t)get_orientation_index(state->o));
}

static inline bool is_solved(const state_t *state)
{
    for (uint8_t i = 0; i < CUBIES; ++i)
        if (state->p[i] != i || state->o[i] != 0)
            return false;
    return true;
}

#ifndef MINIRUBIK_EMBEDDED
static inline state_t quarter_turn(state_t state, uint8_t face)
// face: 0 = R, 1 = B, 2 = D, which face that we want to do a rotation
{
    state_t result;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t from = source[face][i];
        result.p[i] = state.p[from];
        result.o[i] = mod3[state.o[from] + twist[face][i]];
    }
    return result;
}

#endif

/* One seven-cubie pass for all nine moves, including half/inverse turns. */
static inline state_t apply_move(state_t state, uint8_t move)
{
    state_t result;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t from = move_source[move][i];
        result.p[i] = state.p[from];
        result.o[i] = mod3[state.o[from] + move_twist[move][i]];
    }
    return result;
}

/* Coordinate DFS: each child needs two transition lookups, no cubie mapping
 * or re-ranking. Explicit frames replace recursive calls. Each depth retains its state
 * and next successor cursor, so popping restores the parent without undo.
 * Static storage: no heap or recursive call stack; not reentrant/thread-safe. */
typedef struct {
    uint16_t p; // permutation coordinate: 0..5039
    uint16_t o; // orientation coordinate: 0..728
    uint8_t last_move; // the last move that was applied to the cube (ROOT_MOVE=9, the first step is to try 9 moves)
    uint8_t next_successor; // the index of the next successor to be explored
} search_frame_t;
static search_frame_t search_stack[MAX_DEPTH + 1];
/*
search_stack[0]  initial state
search_stack[1]  the state after the first move
……
search_stack[11] the state after the 11th move
*/

static int dfs_search(const state_t *state, uint8_t g, uint8_t bound,
                      uint8_t last_move, uint8_t path[MAX_DEPTH])
/*
state: the current state of the cube
g: the current depth of the search
bound: the maximum depth of the search (the maximum number of moves that can be applied to the cube)
last_move: the last move that was applied to the cube (ROOT_MOVE=9, the first step is to try 9 moves)
path: the array that stores the moves that have been applied to the cube
*/
{
    if (g > MAX_DEPTH || bound > MAX_DEPTH || last_move > ROOT_MOVE)
        return -1;
    const uint8_t root_depth = g;
    // Push the initial state onto the stack
    search_stack[g].p = (uint16_t)get_permutation_index(state->p);
    search_stack[g].o = (uint16_t)get_orientation_index(state->o);
    search_stack[g].last_move = last_move;
    search_stack[g].next_successor = 0;
    for (;;) {
        search_frame_t *frame = &search_stack[g];
        /* Evaluate a frame once on entry, not again after every child. */
        if (frame->next_successor == 0) { // next_successor == 0 means that we have not evaluated the current state yet
            uint8_t h = coordinate_heuristic(frame->p, frame->o);
            if (g + h > bound)
                goto pop_frame;
            if (frame->p == 0 && frame->o == 0)
                return g; /* Total steps from the initial input. */
            if (g >= bound || g >= MAX_DEPTH)
                goto pop_frame;
        }
        if (frame->next_successor == successor_count[frame->last_move])
        // if all (6 or 9) successors have been explored, we need to pop the current frame and return to the parent frame
            goto pop_frame;
        {
            // Select the next successor to explore, push it onto the stack, and continue the search
            // Then we will explore the next successor in the next iteration of the loop (next_surcessor will be incremented)
            uint8_t move = valid_next_moves[frame->last_move][frame->next_successor++];
            search_frame_t *child = &search_stack[g + 1];
            path[g] = move;
            child->p = permutation_transition[move][frame->p];
            child->o = orientation_transition[move][frame->o];
            child->last_move = move;
            child->next_successor = 0;
            ++g; /* Push: visit the child before trying another sibling. */
        }
        continue;
pop_frame:
        if (g == root_depth)
            return -1;
        --g; // Pop: return to the parent frame
    }
}

static int ida_star(const state_t *state, uint8_t path[MAX_DEPTH])
{
    for (uint8_t bound = evaluate_heuristic(state); bound <= MAX_DEPTH; ++bound) {
        // If we cannot find the answer in this branch, we need to redo dfs and make ++bound
        int length = dfs_search(state, 0, bound, ROOT_MOVE, path);
        if (length >= 0)
            return length;
    }
    return -1;
}

static bool parse_state(const char *input, state_t *state)
{
    uint32_t seen = 0;
    uint8_t sum = 0;
    /* Stop at an early NUL before reading any later input position. */
    for (uint8_t i = 0; i < 14; ++i) {
        if (input[i] == '\0')
            return false;
    }
    if (input[14] != '\0')
        return false;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        if (input[i] < '1' || input[i] > '7' ||
            input[i + CUBIES] < '1' || input[i + CUBIES] > '3')
            return false;
        state->p[i] = (uint8_t)(input[i] - '1');
        state->o[i] = (uint8_t)(input[i + CUBIES] - '1');
        uint32_t bit = 1U << state->p[i];
        if (seen & bit)
            return false;
        seen |= bit;
        sum = (uint8_t)(sum + state->o[i]);
    }
    return mod3[sum] == 0;
}

#ifndef MINIRUBIK_EMBEDDED
static bool self_test(void)
{
    const state_t solved = {{0,1,2,3,4,5,6}, {0}};
    const uint8_t packed[] = {0xa3, 0x5c};
    if (get_pdb_distance(packed, 0) != 3 || get_pdb_distance(packed, 1) != 10 ||
        get_pdb_distance(packed, 2) != 12 || get_pdb_distance(packed, 3) != 5 ||
        evaluate_heuristic(&solved) != 0)
        return false;
    for (uint8_t move = 0; move < MOVES; ++move) {
        state_t state = apply_move(solved, move);
        uint8_t path[MAX_DEPTH];
        if (evaluate_heuristic(&state) != 1 || ida_star(&state, path) != 1)
            return false;
        state = apply_move(state, inverse_move[move]);
        if (!is_solved(&state))
            return false;
    }
    return true;
}

#endif

/* Large exact-distance oracle is host-only, never used by the search.
 * Define MINIRUBIK_EMBEDDED to exclude exhaustive host verification. */
#ifndef MINIRUBIK_EMBEDDED
#include "tests/host_gates.h"
#endif

#ifndef MINIRUBIK_EMBEDDED
static int output_failed(void)
{
    return fflush(stdout) != 0 || ferror(stdout);
}

#endif

#ifdef MINIRUBIK_EMBEDDED
static const char target_input[] = "21345671111111";
int main(void)
{
    state_t state;
    uint8_t path[MAX_DEPTH];
    if (!parse_state(target_input, &state))
        return 2;
    int length = ida_star(&state, path);
    return length < 0 ? 1 : 0;
}
#else
int main(int argc, char **argv)
{
    state_t state;
    uint8_t path[MAX_DEPTH];
    if (argc == 2 && strcmp(argv[1], "--self-test-quick") == 0) {
        if (!self_test()) {
            fputs("self-test failed\n", stderr);
            return 1;
        }
        puts("IDA* self-test passed; packed PDBs: 2885 bytes");
        return output_failed();
    }
#ifndef MINIRUBIK_EMBEDDED
    if (argc == 2 && (strcmp(argv[1], "--self-test") == 0 ||
                     strcmp(argv[1], "--self-test-fast") == 0)) {
        if (!self_test() || !host_gates(strcmp(argv[1], "--self-test") == 0, 0, 1)) {
            fputs("host gates failed\n", stderr);
            return 1;
        }
        return output_failed();
    }
    /* Optional host process sharding; each process has its own static stack. */
    if (argc == 4 && strcmp(argv[1], "--self-test-shard") == 0) {
        char *end_a, *end_b;
        unsigned long shard = strtoul(argv[2], &end_a, 10);
        unsigned long shards = strtoul(argv[3], &end_b, 10);
        if (!argv[2][0] || !argv[3][0] || *end_a || *end_b ||
            shards == 0 || shards > 64 || shard >= shards)
            return 2;
        if (!self_test() || !host_gates(true, (unsigned)shard, (unsigned)shards))
            return 1;
        return output_failed();
    }
#endif
    if (argc != 2 || !parse_state(argv[1], &state)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "solver");
        return 2;
    }
    int length = ida_star(&state, path);
    if (length < 0) {
        fputs("no solution within 11 moves\n", stderr);
        return 1;
    }
    for (int i = 0; i < length; ++i)
        printf("%s%s", i ? " " : "", move_names[path[i]]);
    putchar('\n');
    return output_failed();
}
#endif /* MINIRUBIK_EMBEDDED */
