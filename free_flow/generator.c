/*
 * generator.c — Numberlink puzzle generator
 *
 * Algorithm: "Hamiltonian Path + Split" (solution-first, 2×2-free by design)
 * ──────────────────────────────────────────────────────────────────────────
 * Phase 1 — Hamiltonian path
 *   Find a random Hamiltonian path through all r×c cells using a DFS guided
 *   by Warnsdorff's heuristic (always try the neighbor with fewest onward
 *   moves first).  Random tie-breaking gives variety.
 *
 * Phase 2 — Split
 *   Randomly choose (num_colors - 1) split points in the path to divide it
 *   into num_colors sub-paths, each at least 2 cells long.
 *   Reject splits where the filled solution grid contains a 2×2 same-color
 *   block.  Retry with different split points up to MAX_SPLIT_TRIES times.
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
 * backtracking (O(n²) steps).  If we exceed rows*cols * 4 nodes, something
 * went wrong; abort and retry from a different start.
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
static int warnsdorff_degree(int vis[MAX_ROWS][MAX_COLS],
                              int rows, int cols, int r, int c)
{
    int deg = 0;
    for (int d = 0; d < 4; d++) {
        int nr = r + DR[d], nc = c + DC[d];
        if (nr >= 0 && nr < rows && nc >= 0 && nc < cols && !vis[nr][nc])
            deg++;
    }
    return deg;
}

/* ── Hamiltonian path via DFS + Warnsdorff ─────────────────────────────── */

static long g_ham_nodes;

static int dfs_hamiltonian(int path_r[], int path_c[],
                            int vis[MAX_ROWS][MAX_COLS],
                            int rows, int cols, int step, int total)
{
    if (step == total) return 1;
    if (++g_ham_nodes > HAM_NODE_BUDGET) return 0;

    int cr = path_r[step - 1], cc = path_c[step - 1];

    /* Collect unvisited neighbors */
    int nrs[4], ncs[4], degs[4], nn = 0;
    for (int d = 0; d < 4; d++) {
        int nr = cr + DR[d], nc = cc + DC[d];
        if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
        if (vis[nr][nc]) continue;
        nrs[nn] = nr; ncs[nn] = nc;
        degs[nn] = warnsdorff_degree(vis, rows, cols, nr, nc);
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

        if (dfs_hamiltonian(path_r, path_c, vis, rows, cols, step + 1, total))
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
static int find_hamiltonian(int path_r[], int path_c[], int rows, int cols)
{
    int total = rows * cols;

    /* Random starting cell */
    int sr = rand() % rows, sc = rand() % cols;

    int vis[MAX_ROWS][MAX_COLS];
    memset(vis, 0, sizeof(vis));

    path_r[0] = sr; path_c[0] = sc;
    vis[sr][sc] = 1;

    g_ham_nodes = 0;
    return dfs_hamiltonian(path_r, path_c, vis, rows, cols, 1, total);
}

/* ── 2×2 check for sub-paths ───────────────────────────────────────────── */

/*
 * subpath_has_2x2()
 *
 * Returns 1 if consecutive cells [start..end) in the Hamiltonian path
 * contain a "square": three consecutive steps that together with the
 * first cell form a 2×2 block.
 */
static int subpath_has_2x2(const int path_r[], const int path_c[],
                            int start, int end)
{
    for (int i = start; i + 3 < end; i++) {
        int r0 = path_r[i],   c0 = path_c[i];
        int r1 = path_r[i+1], c1 = path_c[i+1];
        int r2 = path_r[i+2], c2 = path_c[i+2];
        int r3 = path_r[i+3], c3 = path_c[i+3];

        int rmin = r0, rmax = r0, cmin = c0, cmax = c0;
        rmin = r1 < rmin ? r1 : rmin; rmax = r1 > rmax ? r1 : rmax;
        rmin = r2 < rmin ? r2 : rmin; rmax = r2 > rmax ? r2 : rmax;
        rmin = r3 < rmin ? r3 : rmin; rmax = r3 > rmax ? r3 : rmax;
        cmin = c1 < cmin ? c1 : cmin; cmax = c1 > cmax ? c1 : cmax;
        cmin = c2 < cmin ? c2 : cmin; cmax = c2 > cmax ? c2 : cmax;
        cmin = c3 < cmin ? c3 : cmin; cmax = c3 > cmax ? c3 : cmax;

        if (rmax - rmin == 1 && cmax - cmin == 1)
            return 1;
    }
    return 0;
}

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
 */
static int greedy_splits(const int bad[], int nb, int nc, int total,
                          int splits[])
{
    int ns = 0;
    int last_split = 0;
    int gi = 0;

    while (gi < nb) {
        int i = bad[gi];

        int lo = i + 1;
        int hi = i + 3;
        if (hi > total - 3) hi = total - 3;
        if (lo > hi) return -1;

        if (lo < last_split + 3) lo = last_split + 3;
        if (lo > hi) return -1;

        if (ns >= nc - 1) return -1;

        int k = lo + rand() % (hi - lo + 1);
        splits[ns++] = k;
        last_split = k;

        while (gi < nb && bad[gi] < k) gi++;
    }

    return ns;
}

static int try_generate(Puzzle *p, int rows, int cols, int nc)
{
    int total = rows * cols;
    int path_r[MAX_ROWS * MAX_COLS], path_c[MAX_ROWS * MAX_COLS];

    /* Phase 1: Find a Hamiltonian path */
    if (!find_hamiltonian(path_r, path_c, rows, cols))
        return 0;

    /* Phase 2: Identify 2×2 bad positions */
    int bad[MAX_ROWS * MAX_COLS];
    int nb = find_bad_positions(path_r, path_c, total, bad);

    if (nb > nc - 1)
        return 0;

    /* Phase 3: Build cuts[] = nc-1 split points */
    for (int attempt = 0; attempt < MAX_SPLIT_TRIES; attempt++) {

        int gsplits[MAX_COLORS];
        int ns = greedy_splits(bad, nb, nc, total, gsplits);
        if (ns < 0) return 0;

        int extra_needed = (nc - 1) - ns;

        int cuts[MAX_COLORS];
        for (int i = 0; i < ns; i++) cuts[i] = gsplits[i];

        if (extra_needed > 0) {
            int used[MAX_ROWS * MAX_COLS];
            memset(used, 0, total * sizeof(int));
            for (int i = 0; i < ns; i++) used[gsplits[i]] = 1;

            int pool[MAX_ROWS * MAX_COLS], npool = 0;
            for (int i = 3; i <= total - 3; i++)
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

        /* Validate minimum length and non-adjacent endpoints */
        int ok = 1, prev = 0;
        for (int i = 0; i <= ncuts && ok; i++) {
            int end = (i < ncuts) ? cuts[i] : total;
            if (end - prev < 3) { ok = 0; break; }
            int r0 = path_r[prev], c0 = path_c[prev];
            int r1 = path_r[end-1], c1 = path_c[end-1];
            if (abs(r0-r1) + abs(c0-c1) == 1) ok = 0;
            prev = end;
        }
        if (!ok) continue;

        /* Final path-level 2×2 check */
        if (!split_valid(path_r, path_c, cuts, nc, total))
            continue;

        /* Valid split found — build the puzzle */
        p->rows       = rows;
        p->cols       = cols;
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

        /* Reject if the solution grid is structurally invalid.
         *
         * Check 1 — 2×2 same-color blocks.
         * subpath_has_2x2() misses paths that fill two adjacent columns/rows
         * (a U-shape), which create 2×2 blocks in the grid without any 4
         * consecutive path cells fitting in a 2×2 bounding box.
         *
         * Check 2 — T-junctions (3+ same-color neighbors for one cell).
         * The Hamiltonian path can snake back near itself so that a
         * non-consecutive segment of the same sub-path becomes grid-adjacent
         * to another cell of that path.  A valid Numberlink path is a simple
         * chain: every interior cell has exactly 2 same-color neighbors and
         * every endpoint has exactly 1.  Three or more same-color neighbors
         * means paths branch, which is invalid. */
        int grid_ok = 1;
        for (int r = 0; r < rows && grid_ok; r++) {
            for (int c = 0; c < cols && grid_ok; c++) {
                int v = p->solution[r][c];

                /* 2×2 block check */
                if (r + 1 < rows && c + 1 < cols &&
                    v == p->solution[r][c+1] &&
                    v == p->solution[r+1][c] &&
                    v == p->solution[r+1][c+1])
                    grid_ok = 0;

                /* T-junction check */
                int same = 0;
                if (r > 0      && p->solution[r-1][c] == v) same++;
                if (r < rows-1 && p->solution[r+1][c] == v) same++;
                if (c > 0      && p->solution[r][c-1] == v) same++;
                if (c < cols-1 && p->solution[r][c+1] == v) same++;
                if (same >= 3) grid_ok = 0;
            }
        }
        if (!grid_ok) continue;

        return 1;
    }

    return 0;
}

/* ── Public interface ──────────────────────────────────────────────────── */

int generate_puzzle(Puzzle *p, int rows, int cols, int num_colors)
{
    if (rows < 2 || rows > MAX_ROWS)               return 0;
    if (cols < 2 || cols > MAX_COLS)               return 0;
    if (num_colors < 2 || num_colors > MAX_COLORS) return 0;
    if (num_colors * 2 > rows * cols)              return 0;

    for (int attempt = 0; attempt < MAX_GEN_ATTEMPTS; attempt++) {
        if (try_generate(p, rows, cols, num_colors))
            return 1;
    }
    return 0;
}
