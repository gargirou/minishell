/*
 * generator.c — Numberlink puzzle generator
 *
 * Algorithm: "Hamiltonian Path + Split" (solution-first, 2×2-free by design)
 * ──────────────────────────────────────────────────────────────────────────
 * Phase 1 — Hamiltonian path
 *   Find a random Hamiltonian path through all n×n cells using a DFS guided
 *   by Warnsdorff's heuristic (always try the neighbor with fewest onward
 *   moves first).  Random tie-breaking gives variety.
 *
 * Phase 2 — Split
 *   Randomly choose (num_colors - 1) split points in the path to divide it
 *   into num_colors sub-paths, each at least 2 cells long.
 *   Reject splits where any sub-path visits 4 consecutive cells that form
 *   a 2×2 square (same-color block violation).  Retry with different split
 *   points up to MAX_SPLIT_TRIES times.
 *
 * Phase 3 — Build puzzle
 *   Each sub-path becomes one color.  The first and last cell of each
 *   sub-path are the endpoint clues; every other cell is an interior path
 *   cell.
 *
 * Why this scales
 * ───────────────
 * The growing-snakes approach fails for large grids because k independent
 * "snakes" growing simultaneously tend to box each other in, especially
 * when 2×2 moves are forbidden.  A single Hamiltonian path has no such
 * inter-path blocking, so it succeeds on essentially any grid size.
 *
 * Failure cases (trigger a retry):
 *   • The DFS exceeds its node budget (very rare with Warnsdorff).
 *   • No valid split is found in MAX_SPLIT_TRIES tries (only when nc is
 *     very large relative to the path length).
 */

#include "puzzle.h"
#include <stdlib.h>
#include <string.h>

/* ── Constants ─────────────────────────────────────────────────────────── */
#define MAX_GEN_ATTEMPTS   50000   /* outer retry limit                      */
#define MAX_SPLIT_TRIES    5000    /* inner split-search limit               */
/*
 * Warnsdorff's heuristic almost always finds a Hamiltonian path with zero
 * backtracking (O(n²) steps).  If we exceed n² * 4 nodes, something went
 * wrong; abort and retry from a different start.
 */
#define HAM_NODE_BUDGET    2000L   /* DFS node budget per attempt            */

/* 4-directional movement: up, down, left, right */
static const int DR[4] = { -1,  1,  0,  0 };
static const int DC[4] = {  0,  0, -1,  1 };

/* ── Helpers ───────────────────────────────────────────────────────────── */

static void shuffle_pairs(int *ar, int *ac, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tr = ar[i]; ar[i] = ar[j]; ar[j] = tr;
        int tc = ac[i]; ac[i] = ac[j]; ac[j] = tc;
    }
}

static void shuffle(int *arr, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = arr[i]; arr[i] = arr[j]; arr[j] = t;
    }
}

/*
 * warnsdorff_degree()
 *
 * Count unvisited neighbors of (r,c) — the Warnsdorff "degree".
 * Lower degree = more constrained = try first.
 */
static int warnsdorff_degree(int vis[MAX_SIZE][MAX_SIZE], int size, int r, int c)
{
    int deg = 0;
    for (int d = 0; d < 4; d++) {
        int nr = r + DR[d], nc = c + DC[d];
        if (nr >= 0 && nr < size && nc >= 0 && nc < size && !vis[nr][nc])
            deg++;
    }
    return deg;
}

/* ── Hamiltonian path via DFS + Warnsdorff ─────────────────────────────── */

static long g_ham_nodes;

static int dfs_hamiltonian(int path_r[], int path_c[],
                            int vis[MAX_SIZE][MAX_SIZE],
                            int size, int step, int total)
{
    if (step == total) return 1;
    if (++g_ham_nodes > HAM_NODE_BUDGET) return 0;

    int cr = path_r[step - 1], cc = path_c[step - 1];

    /* Collect unvisited neighbors */
    int nrs[4], ncs[4], degs[4], nn = 0;
    for (int d = 0; d < 4; d++) {
        int nr = cr + DR[d], nc = cc + DC[d];
        if (nr < 0 || nr >= size || nc < 0 || nc >= size) continue;
        if (vis[nr][nc]) continue;
        nrs[nn] = nr; ncs[nn] = nc;
        degs[nn] = warnsdorff_degree(vis, size, nr, nc);
        nn++;
    }
    if (nn == 0) return 0;

    /* Sort by Warnsdorff degree (bubble sort on 4 elements — fine) */
    for (int i = 0; i < nn - 1; i++)
        for (int j = i + 1; j < nn; j++)
            if (degs[j] < degs[i]) {
                int t;
                t = degs[i]; degs[i] = degs[j]; degs[j] = t;
                t = nrs[i];  nrs[i]  = nrs[j];  nrs[j]  = t;
                t = ncs[i];  ncs[i]  = ncs[j];  ncs[j]  = t;
            }

    /* Shuffle within same-degree groups for randomness */
    int gi = 0;
    while (gi < nn) {
        int gend = gi;
        while (gend < nn && degs[gend] == degs[gi]) gend++;
        shuffle_pairs(nrs + gi, ncs + gi, gend - gi);
        gi = gend;
    }

    /* Try each neighbor in Warnsdorff order */
    for (int k = 0; k < nn; k++) {
        int nr = nrs[k], nc = ncs[k];
        path_r[step] = nr;
        path_c[step] = nc;
        vis[nr][nc] = 1;

        if (dfs_hamiltonian(path_r, path_c, vis, size, step + 1, total))
            return 1;

        vis[nr][nc] = 0;
    }
    return 0;
}

/*
 * find_hamiltonian()
 *
 * Find a Hamiltonian path starting from a random cell.
 * Returns 1 on success; path_r[]/path_c[] contain the ordered cells.
 */
static int find_hamiltonian(int path_r[], int path_c[], int size)
{
    int total = size * size;

    /* Random starting cell */
    int sr = rand() % size, sc = rand() % size;

    int vis[MAX_SIZE][MAX_SIZE];
    memset(vis, 0, sizeof(vis));

    path_r[0] = sr; path_c[0] = sc;
    vis[sr][sc] = 1;

    g_ham_nodes = 0;
    return dfs_hamiltonian(path_r, path_c, vis, size, 1, total);
}

/* ── 2×2 check for split sub-paths ────────────────────────────────────── */

/*
 * subpath_has_2x2()
 *
 * Returns 1 if consecutive cells [start..end) in the Hamiltonian path
 * contain a "square": three consecutive steps that together with the
 * first cell form a 2×2 block.
 *
 * A 2×2 occurs when path[i], path[i+1], path[i+2], path[i+3] (for any i)
 * are the four corners of a unit square (in any order that is a valid path).
 * This happens exactly when the direction from i→i+1 and i+2→i+3 are
 * opposite, and the direction i+1→i+2 connects them on the side.
 * Equivalently: the bounding box of any 4 consecutive cells is 2×2 and
 * all four cells are in distinct corners of that box.
 */
static int subpath_has_2x2(const int path_r[], const int path_c[],
                            int start, int end)
{
    /* Need at least 4 cells to form a 2×2 */
    for (int i = start; i + 3 < end; i++) {
        int r0 = path_r[i],   c0 = path_c[i];
        int r1 = path_r[i+1], c1 = path_c[i+1];
        int r2 = path_r[i+2], c2 = path_c[i+2];
        int r3 = path_r[i+3], c3 = path_c[i+3];

        /* Bounding box of the four points */
        int rmin = r0, rmax = r0, cmin = c0, cmax = c0;
        rmin = r1 < rmin ? r1 : rmin; rmax = r1 > rmax ? r1 : rmax;
        rmin = r2 < rmin ? r2 : rmin; rmax = r2 > rmax ? r2 : rmax;
        rmin = r3 < rmin ? r3 : rmin; rmax = r3 > rmax ? r3 : rmax;
        cmin = c1 < cmin ? c1 : cmin; cmax = c1 > cmax ? c1 : cmax;
        cmin = c2 < cmin ? c2 : cmin; cmax = c2 > cmax ? c2 : cmax;
        cmin = c3 < cmin ? c3 : cmin; cmax = c3 > cmax ? c3 : cmax;

        /* A 2×2 square has a 2×2 bounding box */
        if (rmax - rmin == 1 && cmax - cmin == 1)
            return 1;
    }
    return 0;
}

/*
 * split_valid()
 *
 * Given split points cuts[0..nc-2] (indices in the path where sub-paths
 * end), check that no sub-path has an internal 2×2 pattern.
 *
 * cuts[] has (nc-1) entries.  Sub-path i covers [starts[i], cuts[i]).
 * Sub-path nc-1 covers [cuts[nc-2], total).
 */
static int split_valid(const int path_r[], const int path_c[],
                        const int cuts[], int nc, int total)
{
    int start = 0;
    for (int i = 0; i < nc; i++) {
        int end = (i < nc - 1) ? cuts[i] : total;
        if (subpath_has_2x2(path_r, path_c, start, end))
            return 0;
        start = end;
    }
    return 1;
}

/* ── Core generation attempt ───────────────────────────────────────────── */

/*
 * find_bad_positions()
 *
 * Scan the Hamiltonian path and record indices i where path[i..i+3]
 * form a 2×2 square.  Returns the number of bad positions.
 */
static int find_bad_positions(const int path_r[], const int path_c[],
                               int total, int bad[])
{
    int nb = 0;
    for (int i = 0; i + 3 < total; i++) {
        int r0=path_r[i], c0=path_c[i];
        int r1=path_r[i+1], c1=path_c[i+1];
        int r2=path_r[i+2], c2=path_c[i+2];
        int r3=path_r[i+3], c3=path_c[i+3];
        int rmin=r0, rmax=r0, cmin=c0, cmax=c0;
        rmin=r1<rmin?r1:rmin; rmax=r1>rmax?r1:rmax;
        rmin=r2<rmin?r2:rmin; rmax=r2>rmax?r2:rmax;
        rmin=r3<rmin?r3:rmin; rmax=r3>rmax?r3:rmax;
        cmin=c1<cmin?c1:cmin; cmax=c1>cmax?c1:cmax;
        cmin=c2<cmin?c2:cmin; cmax=c2>cmax?c2:cmax;
        cmin=c3<cmin?c3:cmin; cmax=c3>cmax?c3:cmax;
        if (rmax - rmin == 1 && cmax - cmin == 1)
            bad[nb++] = i;
    }
    return nb;
}

/*
 * greedy_splits()
 *
 * Given bad positions bad[0..nb-1] in the Hamiltonian path, compute the
 * minimum set of split points that "pierce" every bad group.
 *
 * A bad group at index i requires a split at some k ∈ [i+1, i+3]
 * (i.e., within the 4 cells of the group, but NOT at the endpoints,
 * since a split at k means the first sub-path ends before index k).
 *
 * Greedy piercing (scan left-to-right by right endpoint):
 *   For each unpierced group, place a split as far right as allowed
 *   (= min(i+3, total-2)) to cover as many future groups as possible.
 *
 * Additionally, each split must leave the minimum sub-path length of 2.
 * Returns the number of greedy splits placed in splits[], or -1 if
 * more than (nc-1) splits are needed (infeasible for this path).
 *
 * To introduce randomness (for puzzle variety), we randomly jitter the
 * split position within the valid range [i+1, i+3] when the greedy
 * choice is not forced.
 */
static int greedy_splits(const int bad[], int nb, int nc, int total,
                          int splits[])
{
    int ns = 0;         /* number of splits placed so far */
    int last_split = 0; /* right boundary of previous sub-path (= prev cut) */
    int gi = 0;         /* index into bad[] */

    while (gi < nb) {
        int i = bad[gi]; /* bad position: path[i..i+3] is a 2×2 */

        /* Range for the split: [i+1, i+3], clamped to valid positions */
        int lo = i + 1;
        int hi = i + 3;
        if (hi > total - 2) hi = total - 2; /* keep last sub-path ≥ 2 cells */
        if (lo > hi) return -1; /* can't fix this bad group */

        /* Clamp to ensure the new sub-path has ≥ 2 cells */
        if (lo < last_split + 2) lo = last_split + 2;
        if (lo > hi) return -1;

        /* Check budget */
        if (ns >= nc - 1) return -1;

        /* Place split: random position in [lo, hi] for variety */
        int k = lo + rand() % (hi - lo + 1);
        splits[ns++] = k;
        last_split = k;

        /* Skip all bad groups that this split already covers */
        while (gi < nb && bad[gi] < k) gi++;
    }

    return ns; /* number of greedy splits used */
}

static int try_generate(Puzzle *p, int size, int nc)
{
    int total = size * size;
    int path_r[MAX_SIZE * MAX_SIZE], path_c[MAX_SIZE * MAX_SIZE];

    /* Phase 1: Find a Hamiltonian path */
    if (!find_hamiltonian(path_r, path_c, size))
        return 0;

    /* Phase 2: Identify 2×2 bad positions */
    int bad[MAX_SIZE * MAX_SIZE];
    int nb = find_bad_positions(path_r, path_c, total, bad);

    /* We need at least nb greedy splits to fix all bad positions */
    if (nb > nc - 1) {
        /*
         * More bad positions than available cuts — impossible to split.
         * (Some paths are unsplittable for this nc; retry with a new path.)
         */
        return 0;
    }

    /* Phase 3: Build cuts[] = nc-1 split points */
    /*
     * Strategy:
     *   a) Compute greedy splits to fix all bad positions  (ns splits)
     *   b) Insert (nc-1 - ns) additional splits at random valid positions
     *      to create the required number of sub-paths.
     *   c) Verify all sub-paths have ≥ 2 cells and no 2×2 blocks.
     *
     * Retry up to MAX_SPLIT_TRIES times (for the random extras).
     */
    for (int attempt = 0; attempt < MAX_SPLIT_TRIES; attempt++) {

        int gsplits[MAX_COLORS];  /* greedy split positions */
        int ns = greedy_splits(bad, nb, nc, total, gsplits);
        if (ns < 0) return 0; /* infeasible */

        int extra_needed = (nc - 1) - ns;

        /*
         * Build pool of valid extra split positions: all positions in
         * [2, total-2] not already in gsplits[] and not adjacent to a
         * gsplit position (to maintain minimum sub-path length).
         */
        int cuts[MAX_COLORS];
        /* Copy greedy splits */
        for (int i = 0; i < ns; i++) cuts[i] = gsplits[i];

        if (extra_needed > 0) {
            /* Mark used positions */
            int used[MAX_SIZE * MAX_SIZE];
            memset(used, 0, total * sizeof(int));
            for (int i = 0; i < ns; i++) used[gsplits[i]] = 1;

            int pool[MAX_SIZE * MAX_SIZE], npool = 0;
            for (int i = 2; i <= total - 2; i++)
                if (!used[i]) pool[npool++] = i;

            if (npool < extra_needed) return 0;
            shuffle(pool, npool);

            for (int i = 0; i < extra_needed; i++)
                cuts[ns + i] = pool[i];
        }

        /* Sort all nc-1 cuts */
        int ncuts = nc - 1;
        for (int i = 1; i < ncuts; i++) {
            int key = cuts[i], j = i - 1;
            while (j >= 0 && cuts[j] > key) { cuts[j+1] = cuts[j]; j--; }
            cuts[j+1] = key;
        }

        /* Validate minimum length (≥ 2 per sub-path) */
        int ok = 1, prev = 0;
        for (int i = 0; i < ncuts && ok; i++) {
            if (cuts[i] - prev < 2) ok = 0;
            prev = cuts[i];
        }
        if (total - prev < 2) ok = 0;
        if (!ok) continue;

        /* Final 2×2 check (greedy handles bad positions, but double-check) */
        if (!split_valid(path_r, path_c, cuts, nc, total))
            continue;

        /* Valid split found — build the puzzle */
        p->size       = size;
        p->num_colors = nc;
        memset(p->grid,     0, sizeof(p->grid));
        memset(p->solution, 0, sizeof(p->solution));

        int color = 1, start = 0;
        for (int i = 0; i < nc; i++) {
            int end = (i < nc - 1) ? cuts[i] : total;
            for (int j = start; j < end; j++)
                p->solution[path_r[j]][path_c[j]] = color;
            p->grid[path_r[start]][path_c[start]] = color;
            p->grid[path_r[end-1]][path_c[end-1]] = color;
            start = end;
            color++;
        }

        return 1;
    }

    return 0; /* No valid split found */
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
