/*
 * generator.c — Numberlink puzzle generator
 *
 * Algorithm: "Growing Snakes" (solution-first)
 * ─────────────────────────────────────────────
 * 1. Place num_colors random seed cells, one per color.
 * 2. Repeatedly pick a random extendable path (one whose tail has at
 *    least one unvisited adjacent cell) and advance its tail to a
 *    randomly chosen unvisited neighbor.
 * 3. Continue until every cell in the grid belongs to a path.
 * 4. Expose only the head and tail of each path as the puzzle clues.
 *
 * Failure cases (trigger a retry):
 *   • The growing process gets stuck before all cells are covered
 *     (an empty cell becomes completely surrounded by other paths).
 *   • A path ends up with fewer than 2 cells (head == tail), which
 *     would give overlapping endpoints.
 *
 * Because generation is NP-hard in general, we simply retry with a new
 * random state up to MAX_GEN_ATTEMPTS times.
 */

#include "puzzle.h"
#include <stdlib.h>
#include <string.h>

/* ── Constants ─────────────────────────────────────────────────────────── */
#define MAX_GEN_ATTEMPTS 100000

/* 4-directional movement: up, down, left, right */
static const int DR[4] = { -1,  1,  0,  0 };
static const int DC[4] = {  0,  0, -1,  1 };

/* ── Helpers ───────────────────────────────────────────────────────────── */

/* Fisher-Yates in-place shuffle */
static void shuffle(int *arr, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

/* ── Core generation attempt ───────────────────────────────────────────── */

/*
 * try_generate()
 *
 * One attempt to fill the grid.  Returns 1 on success, 0 on failure.
 * On success, *p is fully populated (grid + solution).
 */
static int try_generate(Puzzle *p, int size, int nc)
{
    int color[MAX_SIZE][MAX_SIZE];
    memset(color, 0, sizeof(color));

    /* Per-path state: head (fixed seed), tail (actively extended) */
    int head_r[MAX_COLORS + 1], head_c[MAX_COLORS + 1];
    int tail_r[MAX_COLORS + 1], tail_c[MAX_COLORS + 1];
    int path_len[MAX_COLORS + 1];
    memset(path_len, 0, sizeof(path_len));

    int total = size * size;

    /* ── Step 1: Place nc random seeds ──────────────────────────────── */
    int cells[MAX_SIZE * MAX_SIZE];
    for (int i = 0; i < total; i++) cells[i] = i;
    shuffle(cells, total);

    for (int i = 1; i <= nc; i++) {
        int r = cells[i - 1] / size;
        int c = cells[i - 1] % size;
        color[r][c] = i;
        head_r[i] = tail_r[i] = r;
        head_c[i] = tail_c[i] = c;
        path_len[i] = 1;
    }

    int unvisited = total - nc;

    /* ── Step 2: Grow tails until every cell is covered ─────────────── */
    while (unvisited > 0) {
        /*
         * Build a list of paths whose tail has at least one free
         * (unvisited) neighbor.  Shuffle for uniform random selection.
         */
        int extendable[MAX_COLORS + 1];
        int ne = 0;

        for (int i = 1; i <= nc; i++) {
            for (int d = 0; d < 4; d++) {
                int nr = tail_r[i] + DR[d];
                int nc2 = tail_c[i] + DC[d];
                if (nr >= 0 && nr < size && nc2 >= 0 && nc2 < size
                        && color[nr][nc2] == 0) {
                    extendable[ne++] = i;
                    break;
                }
            }
        }

        if (ne == 0) return 0; /* Stuck: at least one cell is enclosed */

        shuffle(extendable, ne);
        int path = extendable[rand() % ne];

        /* Collect free neighbors of this path's tail */
        int nrs[4], ncs[4], nn = 0;
        for (int d = 0; d < 4; d++) {
            int nr = tail_r[path] + DR[d];
            int nc2 = tail_c[path] + DC[d];
            if (nr >= 0 && nr < size && nc2 >= 0 && nc2 < size
                    && color[nr][nc2] == 0) {
                nrs[nn] = nr;
                ncs[nn] = nc2;
                nn++;
            }
        }

        /* Extend tail to a random free neighbor */
        int ci = rand() % nn;
        int nr = nrs[ci], nc2 = ncs[ci];
        color[nr][nc2] = path;
        tail_r[path] = nr;
        tail_c[path] = nc2;
        path_len[path]++;
        unvisited--;
    }

    /* ── Step 3: Validate path lengths ──────────────────────────────── */
    /*
     * Every path must have at least 2 cells so that head and tail are
     * distinct cells.  A path of length 1 would place two endpoint clues
     * on the same cell, which is invalid.
     */
    for (int i = 1; i <= nc; i++) {
        if (path_len[i] < 2) return 0;
        /* Redundant safety check */
        if (head_r[i] == tail_r[i] && head_c[i] == tail_c[i]) return 0;
    }

    /* ── Step 4: Build the Puzzle struct ─────────────────────────────── */
    p->size       = size;
    p->num_colors = nc;
    memset(p->grid, 0, sizeof(p->grid));

    /* Full solution: every cell has its color */
    for (int r = 0; r < size; r++)
        for (int c = 0; c < size; c++)
            p->solution[r][c] = color[r][c];

    /* Puzzle clues: only the two endpoints per path */
    for (int i = 1; i <= nc; i++) {
        p->grid[head_r[i]][head_c[i]] = i;
        p->grid[tail_r[i]][tail_c[i]] = i;
    }

    return 1;
}

/* ── Public interface ──────────────────────────────────────────────────── */

int generate_puzzle(Puzzle *p, int size, int num_colors)
{
    /* Bounds checking */
    if (size < 2 || size > MAX_SIZE)               return 0;
    if (num_colors < 2 || num_colors > MAX_COLORS) return 0;
    if (num_colors * 2 > size * size)              return 0;

    for (int attempt = 0; attempt < MAX_GEN_ATTEMPTS; attempt++) {
        if (try_generate(p, size, num_colors))
            return 1;
    }
    return 0; /* Exhausted retries */
}
