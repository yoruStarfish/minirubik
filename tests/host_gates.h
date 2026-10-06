/* Host-only exhaustive verification, included after the search definitions.
 * Oracle uses independent push maps, ordinary arithmetic, and unpacked BFS
 * distances. It never calls the solver's move/index/PDB routines to build BFS.
 */
/*
First, generate a complete BFS distance table
              ↓
H1: Check if the heuristic overestimates
H2, H4: Check table contents and implement batched reads
              ↓
H3: Run IDA* for each state
              ↓
Compare the IDA* solution length with the BFS distance
Replay the solution to verify it actually solves the problem
*/
#ifndef MINIRUBIK_HOST_GATES_H
#define MINIRUBIK_HOST_GATES_H
#ifdef _WIN32
#include <windows.h>
static double gate_wall_time(void)
{
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}
#else
#include <sys/time.h>
static double gate_wall_time(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (double)now.tv_sec + (double)now.tv_usec / 1000000.0;
}
#endif

enum { GATE_STATES = 5040 * 729 };
static const uint8_t oracle_destination[3][7] = {
    {3,0,2,4,1,5,6}, {0,1,2,6,3,4,5}, {0,4,1,3,5,2,6}
};
static const uint8_t oracle_delta[3][7] = {
    {2,1,0,1,2,0,0}, {0,0,0,2,1,2,1}, {0,0,0,0,0,0,0}
};

static state_t oracle_turn(state_t state, unsigned face)
{
    state_t next;
    for (unsigned old = 0; old < 7; ++old) {
        unsigned dest = oracle_destination[face][old];
        next.p[dest] = state.p[old];
        next.o[dest] = (uint8_t)((state.o[old] + oracle_delta[face][old]) % 3);
    }
    return next;
}

static state_t oracle_move(state_t state, unsigned move)
{
    for (unsigned turn = 0; turn <= move % 3; ++turn)
        state = oracle_turn(state, move / 3);
    return state;
}

static uint32_t oracle_p(const state_t *state)
{
    uint32_t rank = 0;
    for (unsigned i = 0; i < 7; ++i) {
        unsigned smaller = 0;
        for (unsigned j = i + 1; j < 7; ++j)
            smaller += state->p[j] < state->p[i];
        rank = rank * (7 - i) + smaller;
    }
    return rank;
}

static uint32_t oracle_o(const state_t *state)
{
    uint32_t rank = 0;
    for (unsigned i = 6; i > 0; --i)
        rank = rank * 3 + state->o[i - 1];
    return rank;
}

static state_t oracle_unrank(uint32_t rank)
{
    state_t state;
    uint8_t available[7] = {0,1,2,3,4,5,6};
    unsigned p = rank / 729, o = rank % 729, weight = 720, sum = 0;
    for (unsigned i = 0; i < 7; ++i) {
        unsigned q = p / weight;
        p %= weight;
        state.p[i] = available[q];
        for (unsigned j = q; j + 1 < 7 - i; ++j)
            available[j] = available[j + 1];
        if (i < 5) weight /= 6 - i;
    }
    for (unsigned i = 0; i < 6; ++i) {
        state.o[i] = (uint8_t)(o % 3);
        sum += state.o[i];
        o /= 3;
    }
    state.o[6] = (uint8_t)((3 - sum % 3) % 3);
    return state;
}

/* Fully validate all small tables before the search can index through them.
 * Tables such as source and move names have no 'solved entry'; verify their
 * entire contents/semantics instead, including the special root row. */
static bool gate_small_tables(void)
{
    /* Verify that the small tables are consistent with each other and with the
     * definitions of the cube. This includes checking that the powers_of_3,
     * factorial, mod3, source, twist, move_face, move_turns, inverse_move,
     * valid_next_moves, and successor_count tables are correct. */
    static const char *const names[9] = {"R","R2","R'","B","B2","B'","D","D2","D'"};
    unsigned weight = 1;
    for (unsigned i = 0; i < 6; ++i) {
        if (powers_of_3[i] != weight) return false;
        weight *= 3;
    }
    weight = 720;
    for (unsigned i = 0; i < 7; ++i) {
        if (factorial[i] != weight) return false;
        if (i < 5) weight /= 6 - i;
    }
    for (unsigned i = 0; i < 15; ++i)
        if (mod3[i] != i % 3) return false;
    for (unsigned face = 0; face < 3; ++face)
        for (unsigned old = 0; old < 7; ++old) {
            unsigned dest = oracle_destination[face][old];
            if (source[face][dest] != old || twist[face][dest] != oracle_delta[face][old])
                return false;
        }
    for (unsigned m = 0; m < 9; ++m) {
        unsigned inverse = (m / 3) * 3 + (2 - m % 3);
        if (move_face[m] != m / 3 || move_turns[m] != m % 3 + 1 ||
            inverse_move[m] != inverse || strcmp(move_names[m], names[m]) != 0)
            return false;
    }
    /* Check every generated entry against the independent push model. */
    const state_t solved = {{0,1,2,3,4,5,6}, {0}};
    for (unsigned m = 0; m < 9; ++m) {
        state_t expected = oracle_move(solved, m);
        for (unsigned i = 0; i < 7; ++i)
            if (move_source[m][i] != expected.p[i] ||
                move_twist[m][i] != expected.o[i]) return false;
    }
    /* All permutation and orientation projections: by separability this
     * establishes equivalence on every full cube state for all nine moves. */
    for (unsigned r = 0; r < 5040 + 729; ++r) {
        state_t state = oracle_unrank(r < 5040 ? r * 729 : r - 5040);
        for (uint8_t m = 0; m < MOVES; ++m) {
            state_t expected = oracle_move(state, m), repeated = state;
            for (unsigned t = 0; t < move_turns[m]; ++t)
                repeated = quarter_turn(repeated, move_face[m]);
            state_t actual = apply_move(state, m);
            if (memcmp(&actual, &expected, sizeof actual) ||
                memcmp(&actual, &repeated, sizeof actual)) return false;
        }
    }
    unsigned max_p = 0, max_o = 0;
    for (unsigned r = 0; r < 5040 + 729; ++r) {
        bool perm = r < 5040;
        unsigned index = perm ? r : r - 5040;
        state_t state = oracle_unrank(perm ? index * 729 : index);
        for (unsigned m = 0; m < 9; ++m) {
            state_t expected = oracle_move(state, m);
            unsigned actual = perm ? permutation_transition[m][index]
                                   : orientation_transition[m][index];
            unsigned reference = perm ? oracle_p(&expected) : oracle_o(&expected);
            if (actual != reference) return false;
            if (perm && actual > max_p) max_p = actual;
            if (!perm && actual > max_o) max_o = actual;
        }
    }
    if (max_p != 5039 || max_o != 728) return false;
    puts("H2 coordinate transitions: all 51921 entries including solved rows verified; maxima=5039/728");
    puts("H2 direct moves: all 126 entries verified; 51921 projected transitions match independent and repeated-quarter models");
    for (unsigned last = 0; last <= 9; ++last) {
        unsigned seen = 0, count = 0;
        for (unsigned m = 0; m < 9; ++m) {
            if (last == 9 || m / 3 != last / 3) {
                if (valid_next_moves[last][count] != m) return false;
                seen |= 1U << m;
                ++count;
            }
        }
        if (successor_count[last] != count || seen == 0) return false;
        for (unsigned i = count; i < 9; ++i)
            if (valid_next_moves[last][i] != 0) return false; /* padding */
    }
    puts("H2 support tables: all entries verified; maxima: powers=243 factorial=720 mod3=2 source=6 twist=2 face=2 turns=3 inverse=8 successors=8 count=9");
    return true;
}

static bool host_gates(bool run_h3, unsigned shard, unsigned shards)
{
    bool ok = false;
    if (shards == 0 || shards > 64 || shard >= shards) return false;
    uint8_t *distance = NULL; // distance[r] = number of moves from solved state to state r, 0~11, 255=unreachable
    uint32_t *queue = NULL;
    static uint16_t pt[3][5040], ot[3][729];
    uint8_t perm_ref[5040], ori_ref[729];
    double started = gate_wall_time();
    if (!gate_small_tables()) {
        fputs("H2: support table mismatch\n", stderr);
        return false;
    }
    for (uint32_t p = 0; p < 5040; ++p) {
        state_t state = oracle_unrank(p * 729);
        if (oracle_p(&state) != p || get_permutation_index(state.p) != p)
            goto done;
        for (unsigned face = 0; face < 3; ++face) {
            state_t next = oracle_turn(state, face);
            pt[face][p] = (uint16_t)oracle_p(&next);
            if (pt[face][p] >= 5040) goto done;
        }
    }
    for (uint32_t o = 0; o < 729; ++o) {
        state_t state = oracle_unrank(o);
        if (oracle_o(&state) != o || get_orientation_index(state.o) != o)
            goto done;
        for (unsigned face = 0; face < 3; ++face) {
            state_t next = oracle_turn(state, face);
            ot[face][o] = (uint16_t)oracle_o(&next);
            if (ot[face][o] >= 729) goto done;
        }
    }
    distance = malloc(GATE_STATES);
    queue = malloc((size_t)GATE_STATES * sizeof *queue);
    if (!distance || !queue) {
        fputs("Oracle allocation failed\n", stderr);
        goto done;
    }
    memset(distance, 255, GATE_STATES);
    distance[0] = 0;
    queue[0] = 0;
    uint32_t head = 0, tail = 1;
    while (head < tail) {
        uint32_t r = queue[head++], p = r / 729, o = r % 729;
        for (unsigned f = 0; f < 3; ++f) {
            uint32_t np = p, no = o;
            for (unsigned t = 0; t < 3; ++t) {
                np = pt[f][np]; no = ot[f][no];
                uint32_t nr = np * 729 + no;
                if (distance[nr] == 255) {
                    if (tail >= GATE_STATES) goto done;
                    distance[nr] = (uint8_t)(distance[r] + 1);
                    queue[tail++] = nr;
                }
            }
        }
    }
    if (tail != GATE_STATES) goto done;
    memset(perm_ref, 255, sizeof perm_ref);
    memset(ori_ref, 255, sizeof ori_ref);
    unsigned maximum = 0;
    uint32_t histogram[12] = {0};
    const uint32_t expected_histogram[12] =
        {1,9,54,321,1847,9992,50136,227536,870072,1887748,623800,2644};
    for (uint32_t r = 0; r < GATE_STATES; ++r) {
        unsigned d = distance[r], p = r / 729, o = r % 729;
        if (d > 11) goto done;
        ++histogram[d];
        if (d > maximum) maximum = d;
        if (d < perm_ref[p]) perm_ref[p] = (uint8_t)d;
        if (d < ori_ref[o]) ori_ref[o] = (uint8_t)d;
    }
    if (maximum != 11 || distance[0] != 0 ||
        memcmp(histogram, expected_histogram, sizeof histogram) != 0) goto done;
    printf("H2 exact BFS: %u populated states; maximum=%u solved=%u; %.3f s wall\n",
           (unsigned)tail, maximum, distance[0], gate_wall_time() - started);
    /* Projecting exact distances gives independent unpacked PDB references. */
    const uint8_t *packed[2] = {pdb_orientation_packed, pdb_permutation_packed};
    const uint8_t *reference[2] = {ori_ref, perm_ref};
    const unsigned counts[2] = {729, 5040}, maxima[2] = {6, 7};
    const char *names[2] = {"orientation", "permutation"};
    if (sizeof pdb_orientation_packed != 365 || sizeof pdb_permutation_packed != 2520)
        goto done;
    for (unsigned table = 0; table < 2; ++table) {
        unsigned max_value = 0, parity[2] = {0,0};
        for (unsigned i = 0; i < counts[table]; ++i) {
            unsigned value = get_pdb_distance(packed[table], i);
            if (value == 15 || value != reference[table][i]) {
                // If the value is 15, it means that the PDB distance is unreachable, which is incorrect. 
                // If the value does not match the reference value, it means that the PDB distance is incorrect.
                fprintf(stderr, "H2/H4 %s index=%u packed=%u reference=%u\n",
                        names[table], i, value, reference[table][i]);
                goto done;
            }
            ++parity[i & 1];
            if (value > max_value) max_value = value;
        }
        if (max_value != maxima[table] || get_pdb_distance(packed[table], 0) != 0)
            goto done;
        printf("H2/H4 %s: %u populated; maximum=%u solved=0; even=%u odd=%u match unpacked BFS\n",
               names[table], counts[table], max_value, parity[0], parity[1]);
    }
    /* The odd-sized orientation table has an unused upper nibble. */
    if (get_pdb_distance(pdb_orientation_packed, 729) != 15) goto done;
    for (unsigned byte = 0; byte < 256; ++byte) {
        uint8_t value = (uint8_t)byte;
        if (get_pdb_distance(&value, 0) != byte % 16 ||
            get_pdb_distance(&value, 1) != byte / 16) goto done;
    }
    puts("H4 accessor: all 256 byte patterns, both nibbles, and final padding verified");
    started = gate_wall_time();
    for (uint32_t r = 0; r < GATE_STATES; ++r) {
        state_t state = oracle_unrank(r);
        unsigned hp = get_pdb_distance(pdb_permutation_packed, get_permutation_index(state.p)); // permutation PDB distance
        unsigned ho = get_pdb_distance(pdb_orientation_packed, get_orientation_index(state.o)); // orientation PDB distance
        unsigned h = evaluate_heuristic(&state);
        // distance[r] is the exact distance from the solved state to state r, which is computed by BFS
        if (hp > distance[r] || ho > distance[r] || h > distance[r] ||
            h != (hp > ho ? hp : ho)) {
                /*
            Each statement is to evaluate:
            1. hp > distance[r]: if the permutation PDB distance is greater than the exact distance, it means the heuristic is not admissible
            2. ho > distance[r]: if the orientation PDB distance is greater than the exact distance, it means the heuristic is not admissible
            3. h > distance[r]: if the maximum of the two PDB distances is greater than the exact distance, it means the heuristic is not admissible
            4. h != (hp > ho ? hp : ho): if the maximum of the two PDB distances is not equal to the maximum of hp and ho, it means the heuristic is not correctly computed
            */
            fprintf(stderr, "H1 rank=%u h=%u d=%u\n", (unsigned)r, h, distance[r]);
            goto done;
        }
    }
    printf("H1: both components and max heuristic admissible on all %u states; %.3f s wall\n",
           (unsigned)GATE_STATES, gate_wall_time() - started);
    if (!run_h3) {
        puts("H3 SKIPPED (--self-test-fast); run --self-test for all four gates");
        ok = true;
        goto done;
    }
    uint32_t begin = (uint32_t)(((uint64_t)GATE_STATES * shard) / shards);
    uint32_t end = (uint32_t)(((uint64_t)GATE_STATES * (shard + 1)) / shards);
    started = gate_wall_time();
    printf("H3: searching ranks [%u,%u), shard %u/%u (no oracle hints supplied to IDA*)...\n",
           (unsigned)begin, (unsigned)end, shard, shards);
    fflush(stdout);
    for (uint32_t r = begin; r < end; ++r) {
        state_t state = oracle_unrank(r), replay = state;
        struct { uint8_t before, path[MAX_DEPTH], after; } guarded = {0xa5, {0}, 0x5a};
        /*
        H3 Also confirm that:
        - The returned length is correct.
        - The action sequence is indeed solvable.
        - The guard values ​​surrounding the path array remain intact.
        - The input state ID has not changed.
        */
        int length = ida_star(&state, guarded.path);
        if (length != distance[r] || guarded.before != 0xa5 || guarded.after != 0x5a) {
            fprintf(stderr, "H3 rank=%u length=%d exact=%u or path guard damaged\n",
                    (unsigned)r, length, distance[r]);
            goto done;
        }
        for (int i = 0; i < length; ++i) {
            unsigned m = guarded.path[i];
            if (m >= 9) goto done;
            replay = oracle_move(replay, m);
        }
        if (oracle_p(&replay) != 0 || oracle_o(&replay) != 0 ||
            oracle_p(&state) * 729 + oracle_o(&state) != r) {
            fprintf(stderr, "H3 rank=%u replay failed or input changed\n", (unsigned)r);
            goto done;
        }
        if ((r + 1 - begin) % 16384 == 0) {
            printf("H3 progress: %u/%u; %.3f s wall\n", (unsigned)(r + 1 - begin),
                   (unsigned)(end - begin), gate_wall_time() - started);
            fflush(stdout);
        }
    }
    printf("H3 PASS: ranks [%u,%u); %u optimal solutions independently replayed; %.3f s wall\n",
           (unsigned)begin, (unsigned)end, (unsigned)(end - begin), gate_wall_time() - started);
    puts(shards == 1 ? "H1-H4 PASS" : "H1/H2/H4 PASS; H3 shard PASS (not the full domain)");
    ok = true;
done:
    if (!ok) fputs("Gate validation failed (no PASS issued)\n", stderr);
    free(queue);
    free(distance);
    return ok;
}
#endif
