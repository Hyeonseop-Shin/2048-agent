/*
 * 2048 AI Engine
 *
 * Bitboard-based Expectimax agent for the 2048 game.
 * Designed to be compiled as a shared library and called from Python via ctypes.
 *
 * Reference: nneonneo/2048-ai (https://github.com/nneonneo/2048-ai)
 *
 * Board encoding:
 *   - 64-bit integer, 16 nibbles (4 bits each)
 *   - Each nibble stores log2(tile_value), 0 = empty
 *   - Cell (r,c) at bit position 4*(4*r + c)
 *   - Row r at bits [16*r .. 16*r+15]
 */

#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Platform / visibility                                               */
/* ------------------------------------------------------------------ */

#if defined(_WIN32) || defined(__CYGWIN__)
  #define DLL_PUBLIC __declspec(dllexport)
#else
  #if __GNUC__ >= 4
    #define DLL_PUBLIC __attribute__((visibility("default")))
  #else
    #define DLL_PUBLIC
  #endif
#endif

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

typedef uint64_t board_t;
typedef uint16_t row_t;

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

static const board_t ROW_MASK = 0xFFFFULL;
static const board_t COL_MASK = 0x000F000F000F000FULL;

/* Heuristic weights (CMA-ES optimized, from nneonneo/2048-ai) */
static const float SCORE_LOST_PENALTY        = 200000.0f;
static const float SCORE_MONOTONICITY_POWER  = 4.0f;
static const float SCORE_MONOTONICITY_WEIGHT = 47.0f;
static const float SCORE_SUM_POWER           = 3.5f;
static const float SCORE_SUM_WEIGHT          = 11.0f;
static const float SCORE_MERGES_WEIGHT       = 700.0f;
static const float SCORE_EMPTY_WEIGHT        = 270.0f;

/* Expectimax */
static const float CPROB_THRESH = 0.001f;

/* ------------------------------------------------------------------ */
/* Lookup tables                                                       */
/* ------------------------------------------------------------------ */

static row_t   row_left_table [65536];
static row_t   row_right_table[65536];
static board_t col_up_table   [65536];
static board_t col_down_table [65536];
static float   heur_score_table[65536];
static float   score_table[65536];  /* actual score gained from merges */

/* ------------------------------------------------------------------ */
/* Inline helpers                                                      */
/* ------------------------------------------------------------------ */

static inline board_t transpose(board_t x) {
    board_t a1 = x & 0xF0F00F0FF0F00F0FULL;
    board_t a2 = x & 0x0000F0F00000F0F0ULL;
    board_t a3 = x & 0x0F0F00000F0F0000ULL;
    board_t a  = a1 | (a2 << 12) | (a3 >> 12);
    board_t b1 = a & 0xFF00FF0000FF00FFULL;
    board_t b2 = a & 0x00FF00FF00000000ULL;
    board_t b3 = a & 0x00000000FF00FF00ULL;
    return b1 | (b2 >> 24) | (b3 << 24);
}

static inline int count_empty(board_t board) {
    int count = 0;
    for (int i = 0; i < 16; ++i) {
        if (((board >> (i * 4)) & 0xF) == 0) count++;
    }
    return count;
}

static inline board_t unpack_col(row_t row) {
    board_t tmp = (board_t)row;
    return (tmp | (tmp << 12ULL) | (tmp << 24ULL) | (tmp << 36ULL)) & COL_MASK;
}

static inline row_t reverse_row(row_t row) {
    return (row >> 12) | ((row >> 4) & 0x00F0)
         | ((row << 4) & 0x0F00) | (row << 12);
}

static inline int count_distinct_tiles(board_t board) {
    uint16_t seen = 0;
    while (board) {
        int rank = (int)(board & 0xF);
        if (rank > 0) seen |= (1 << rank);
        board >>= 4;
    }
    /* popcount of seen */
    int count = 0;
    while (seen) {
        count += seen & 1;
        seen >>= 1;
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Table initialization                                                */
/* ------------------------------------------------------------------ */

DLL_PUBLIC void init_tables(void) {
    for (unsigned row = 0; row < 65536; ++row) {
        /* Extract the 4 nibbles */
        unsigned line[4] = {
            (row >>  0) & 0xF,
            (row >>  4) & 0xF,
            (row >>  8) & 0xF,
            (row >> 12) & 0xF,
        };

        /* --- Heuristic score for this row --- */
        float sum = 0.0f;
        int empty = 0;
        int merges = 0;
        int prev = 0;
        int counter = 0;

        for (int i = 0; i < 4; ++i) {
            int rank = (int)line[i];
            sum += powf((float)rank, SCORE_SUM_POWER);
            if (rank == 0) {
                empty++;
            } else {
                if (prev == rank) {
                    counter++;
                } else if (counter > 0) {
                    merges += 1 + counter;
                    counter = 0;
                }
                prev = rank;
            }
        }
        if (counter > 0) {
            merges += 1 + counter;
        }

        float monotonicity_left = 0.0f;
        float monotonicity_right = 0.0f;
        for (int i = 1; i < 4; ++i) {
            if (line[i - 1] > line[i]) {
                monotonicity_left +=
                    powf((float)line[i - 1], SCORE_MONOTONICITY_POWER) -
                    powf((float)line[i],     SCORE_MONOTONICITY_POWER);
            } else {
                monotonicity_right +=
                    powf((float)line[i],     SCORE_MONOTONICITY_POWER) -
                    powf((float)line[i - 1], SCORE_MONOTONICITY_POWER);
            }
        }

        float mono = (monotonicity_left < monotonicity_right)
                     ? monotonicity_left : monotonicity_right;

        heur_score_table[row] =
            SCORE_LOST_PENALTY +
            SCORE_EMPTY_WEIGHT  * (float)empty +
            SCORE_MERGES_WEIGHT * (float)merges -
            SCORE_MONOTONICITY_WEIGHT * mono -
            SCORE_SUM_WEIGHT * sum;

        /* --- Actual merge score for this row --- */
        float sc = 0.0f;
        for (int i = 0; i < 4; ++i) {
            int rank = (int)line[i];
            if (rank >= 2) {
                sc += (float)(rank - 1) * (float)(1 << rank);
            }
        }
        score_table[row] = sc;

        /* --- Move simulation (LEFT) --- */
        unsigned result[4] = { line[0], line[1], line[2], line[3] };

        /* Slide left (remove zeros) */
        int j = 0;
        unsigned compressed[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            if (result[i] != 0) {
                compressed[j++] = result[i];
            }
        }

        /* Merge */
        j = 0;
        unsigned merged[4] = {0, 0, 0, 0};
        int skip = 0;
        for (int i = 0; i < 4; ++i) {
            if (skip) { skip = 0; continue; }
            if (i + 1 < 4 && compressed[i] == compressed[i + 1] && compressed[i] != 0) {
                merged[j++] = compressed[i] + 1;
                skip = 1;
            } else {
                merged[j++] = compressed[i];
            }
        }

        row_t result_row = (row_t)(
            (merged[0] <<  0) |
            (merged[1] <<  4) |
            (merged[2] <<  8) |
            (merged[3] << 12)
        );

        row_t rev_row    = reverse_row((row_t)row);
        row_t rev_result = reverse_row(result_row);

        row_left_table[row]      = (row_t)(row ^ result_row);
        row_right_table[rev_row] = (row_t)(rev_row ^ rev_result);

        col_up_table[row]        = unpack_col((row_t)row) ^ unpack_col(result_row);
        col_down_table[rev_row]  = unpack_col(rev_row)    ^ unpack_col(rev_result);
    }
}

/* ------------------------------------------------------------------ */
/* Move execution                                                      */
/* ------------------------------------------------------------------ */

static inline board_t execute_move_left(board_t board) {
    board_t ret = board;
    ret ^= (board_t)row_left_table[(board >>  0) & ROW_MASK] <<  0;
    ret ^= (board_t)row_left_table[(board >> 16) & ROW_MASK] << 16;
    ret ^= (board_t)row_left_table[(board >> 32) & ROW_MASK] << 32;
    ret ^= (board_t)row_left_table[(board >> 48) & ROW_MASK] << 48;
    return ret;
}

static inline board_t execute_move_right(board_t board) {
    board_t ret = board;
    ret ^= (board_t)row_right_table[(board >>  0) & ROW_MASK] <<  0;
    ret ^= (board_t)row_right_table[(board >> 16) & ROW_MASK] << 16;
    ret ^= (board_t)row_right_table[(board >> 32) & ROW_MASK] << 32;
    ret ^= (board_t)row_right_table[(board >> 48) & ROW_MASK] << 48;
    return ret;
}

static inline board_t execute_move_up(board_t board) {
    board_t ret = board;
    board_t t = transpose(board);
    ret ^= col_up_table[(t >>  0) & ROW_MASK] <<  0;
    ret ^= col_up_table[(t >> 16) & ROW_MASK] <<  4;
    ret ^= col_up_table[(t >> 32) & ROW_MASK] <<  8;
    ret ^= col_up_table[(t >> 48) & ROW_MASK] << 12;
    return ret;
}

static inline board_t execute_move_down(board_t board) {
    board_t ret = board;
    board_t t = transpose(board);
    ret ^= col_down_table[(t >>  0) & ROW_MASK] <<  0;
    ret ^= col_down_table[(t >> 16) & ROW_MASK] <<  4;
    ret ^= col_down_table[(t >> 32) & ROW_MASK] <<  8;
    ret ^= col_down_table[(t >> 48) & ROW_MASK] << 12;
    return ret;
}

DLL_PUBLIC board_t execute_move(board_t board, int move) {
    switch (move) {
        case 0: return execute_move_up(board);
        case 1: return execute_move_right(board);
        case 2: return execute_move_down(board);
        case 3: return execute_move_left(board);
        default: return board;
    }
}

/* ------------------------------------------------------------------ */
/* Heuristic evaluation                                                */
/* ------------------------------------------------------------------ */

static inline float score_helper(board_t board, const float *table) {
    return table[(board >>  0) & ROW_MASK] +
           table[(board >> 16) & ROW_MASK] +
           table[(board >> 32) & ROW_MASK] +
           table[(board >> 48) & ROW_MASK];
}

static float score_heur_board(board_t board) {
    return score_helper(board, heur_score_table) +
           score_helper(transpose(board), heur_score_table);
}

/* ------------------------------------------------------------------ */
/* Transposition table                                                 */
/* ------------------------------------------------------------------ */

/*
 * Simple hash map for caching evaluated board positions.
 * Uses open addressing with linear probing.
 */

#define TRANS_TABLE_SIZE (1 << 22)  /* ~4M entries */
#define TRANS_TABLE_MASK (TRANS_TABLE_SIZE - 1)

typedef struct {
    board_t board;
    uint8_t depth;
    float   heuristic;
} trans_entry_t;

static trans_entry_t trans_table[TRANS_TABLE_SIZE];
static int trans_table_used = 0;

static inline void trans_table_clear(void) {
    memset(trans_table, 0, sizeof(trans_table));
    trans_table_used = 0;
}

static inline uint32_t board_hash(board_t board) {
    /* Simple multiplicative hash */
    board_t h = board;
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDULL;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ULL;
    h ^= h >> 33;
    return (uint32_t)(h & TRANS_TABLE_MASK);
}

static inline int trans_table_lookup(board_t board, int depth, float *out) {
    uint32_t idx = board_hash(board);
    for (int i = 0; i < 16; ++i) {  /* linear probe, max 16 */
        uint32_t pos = (idx + i) & TRANS_TABLE_MASK;
        if (trans_table[pos].board == 0) return 0;  /* empty slot */
        if (trans_table[pos].board == board) {
            if (trans_table[pos].depth <= depth) {
                *out = trans_table[pos].heuristic;
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

static inline void trans_table_store(board_t board, int depth, float heuristic) {
    uint32_t idx = board_hash(board);
    for (int i = 0; i < 16; ++i) {
        uint32_t pos = (idx + i) & TRANS_TABLE_MASK;
        if (trans_table[pos].board == 0 || trans_table[pos].board == board) {
            trans_table[pos].board     = board;
            trans_table[pos].depth     = (uint8_t)depth;
            trans_table[pos].heuristic = heuristic;
            return;
        }
    }
    /* Table full in this neighborhood; overwrite first slot */
    trans_table[idx].board     = board;
    trans_table[idx].depth     = (uint8_t)depth;
    trans_table[idx].heuristic = heuristic;
}

/* ------------------------------------------------------------------ */
/* Expectimax search                                                   */
/* ------------------------------------------------------------------ */

static float score_move_node(board_t board, float cprob, int depth, int depth_limit);
static float score_tilechoose_node(board_t board, float cprob, int depth, int depth_limit);

static float score_tilechoose_node(board_t board, float cprob, int depth, int depth_limit) {
    if (cprob < CPROB_THRESH || depth >= depth_limit) {
        return score_heur_board(board);
    }

    /* Check transposition table */
    float cached;
    if (trans_table_lookup(board, depth, &cached)) {
        return cached;
    }

    int num_empty = count_empty(board);
    if (num_empty == 0) {
        return score_heur_board(board);
    }

    float cprob_per_cell = cprob / (float)num_empty;
    float expected = 0.0f;

    board_t tmp = board;
    board_t tile_mask = 1ULL;

    for (int pos = 0; pos < 16; ++pos) {
        if ((board >> (pos * 4)) & 0xF) {
            continue;  /* cell is not empty */
        }

        /* Place a 2 (rank=1) */
        board_t board_with_2 = board | (1ULL << (pos * 4));
        expected += score_move_node(board_with_2, cprob_per_cell * 0.9f, depth, depth_limit) * 0.9f;

        /* Place a 4 (rank=2) */
        board_t board_with_4 = board | (2ULL << (pos * 4));
        expected += score_move_node(board_with_4, cprob_per_cell * 0.1f, depth, depth_limit) * 0.1f;
    }

    float result = expected / (float)num_empty;

    trans_table_store(board, depth, result);
    return result;
}

static float score_move_node(board_t board, float cprob, int depth, int depth_limit) {
    float best = 0.0f;
    int found_move = 0;

    for (int move = 0; move < 4; ++move) {
        board_t new_board = execute_move(board, move);
        if (new_board == board) continue;  /* invalid move */

        found_move = 1;
        float score = score_tilechoose_node(new_board, cprob, depth + 1, depth_limit);
        if (score > best) {
            best = score;
        }
    }

    if (!found_move) {
        /* Game over */
        return 0.0f;
    }

    return best;
}

static int compute_depth_limit(board_t board) {
    int distinct = count_distinct_tiles(board);
    int empty = count_empty(board);
    int depth_limit = distinct - 2;
    if (depth_limit < 3) depth_limit = 3;
    if (depth_limit > 5) depth_limit = 5;
    /* Allow deeper search when few empty cells (critical situations) */
    if (empty <= 2 && depth_limit < 5) depth_limit = 5;
    return depth_limit;
}

DLL_PUBLIC float score_toplevel_move(board_t board, int move) {
    board_t new_board = execute_move(board, move);
    if (new_board == board) return 0.0f;

    int depth_limit = compute_depth_limit(board);
    return score_tilechoose_node(new_board, 1.0f, 0, depth_limit);
}

DLL_PUBLIC int find_best_move(board_t board) {
    float best_score = -1.0f;
    int best_move = -1;

    int depth_limit = compute_depth_limit(board);

    trans_table_clear();

    for (int move = 0; move < 4; ++move) {
        board_t new_board = execute_move(board, move);
        if (new_board == board) continue;

        float score = score_tilechoose_node(new_board, 1.0f, 0, depth_limit);
        if (score > best_score) {
            best_score = score;
            best_move = move;
        }
    }

    return best_move;
}

/* ------------------------------------------------------------------ */
/* Utility: print board (for debugging)                                */
/* ------------------------------------------------------------------ */

DLL_PUBLIC int get_tile(board_t board, int pos) {
    int rank = (int)((board >> (pos * 4)) & 0xF);
    if (rank == 0) return 0;
    return 1 << rank;
}

/* ------------------------------------------------------------------ */
/* Full game simulation in C (avoids Python loop overhead)             */
/* ------------------------------------------------------------------ */

/* Simple xorshift64 RNG */
static uint64_t rng_state = 12345;

static inline void rng_seed(uint64_t seed) {
    /* Mix the seed with splitmix64 to avoid correlated sequences for close seeds */
    uint64_t z = seed + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    rng_state = z ? z : 1;
}

static inline uint64_t rng_next(void) {
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static board_t c_spawn_tile(board_t board) {
    int empty = count_empty(board);
    if (empty == 0) return board;

    int target = (int)(rng_next() % (uint64_t)empty);
    int val = (rng_next() % 10 < 9) ? 1 : 2;  /* rank 1=tile2, rank 2=tile4 */

    int idx = 0;
    for (int pos = 0; pos < 16; ++pos) {
        if (((board >> (pos * 4)) & 0xF) == 0) {
            if (idx == target) {
                board |= ((board_t)val << (pos * 4));
                return board;
            }
            idx++;
        }
    }
    return board;
}

static int c_is_game_over(board_t board) {
    for (int move = 0; move < 4; ++move) {
        if (execute_move(board, move) != board) return 0;
    }
    return 1;
}

static int c_max_rank(board_t board) {
    int max_r = 0;
    for (int i = 0; i < 16; ++i) {
        int r = (int)((board >> (i * 4)) & 0xF);
        if (r > max_r) max_r = r;
    }
    return max_r;
}

/*
 * Result struct for a single game, packed as 4 int32s for easy ctypes access.
 */
typedef struct {
    int32_t score;
    int32_t max_tile;   /* actual value, e.g. 2048 */
    int32_t moves;
    int32_t time_ms;
} game_result_t;

static game_result_t last_result;
static board_t last_final_board;

DLL_PUBLIC void play_game(uint64_t seed) {
    rng_seed(seed);

    board_t board = 0;
    board = c_spawn_tile(board);
    board = c_spawn_tile(board);

    int32_t score = 0;
    int32_t moves = 0;

    while (!c_is_game_over(board)) {
        int best_move = find_best_move(board);
        if (best_move < 0) break;

        /* Compute score from merge:
         * We track score by computing actual merge points.
         * For each row, compare before and after to find merges.
         */
        board_t new_board = execute_move(board, best_move);
        if (new_board == board) continue;

        /* Calculate merge score: sum of merged tile values */
        /* Simple approach: compute score from the score_table */
        float new_sc = score_helper(new_board, score_table);
        float old_sc = score_helper(board, score_table);
        score += (int32_t)(new_sc - old_sc + 0.5f);

        board = new_board;
        board = c_spawn_tile(board);
        moves++;
    }

    int max_r = c_max_rank(board);
    last_result.score = score;
    last_result.max_tile = (max_r > 0) ? (1 << max_r) : 0;
    last_result.moves = moves;
    last_result.time_ms = 0;  /* caller measures time */
    last_final_board = board;
}

DLL_PUBLIC int32_t get_result_score(void)    { return last_result.score; }
DLL_PUBLIC int32_t get_result_max_tile(void) { return last_result.max_tile; }
DLL_PUBLIC int32_t get_result_moves(void)    { return last_result.moves; }
DLL_PUBLIC board_t get_result_board(void)    { return last_final_board; }


