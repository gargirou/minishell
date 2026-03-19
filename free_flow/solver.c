/*
 * solver.c — Numberlink backtracking solver with BFS pruning
 *
 * Algorithm overview
 * ──────────────────
 * State
 *   filled[r][c]   : 0 = empty, 1..k = color that occupies this cell.
 *                    Both endpoints of every color are pre-filled.
 *   front_r/c[col] : The "active end" of color col — the endpoint of the
 *                    partial path being extended toward the goal.
 *   goal_r/c[col]  : The fixed target endpoint for color col.
 *   done[col]      : 1 once the front has reached the goal.
 *   empty_count    : Number of cells not yet occupied.
 *
 * Transition
 *   Pick the most-constrained incomplete color (fewest valid moves for
 *   its front).  Try extending that front in each of the 4 directions:
 *     • If the neighbor IS the goal → mark done[col] = 1.
 *     • If the neighbor is empty    → fill it with col, decrement empty_count.
 *   Recurse, then undo the change.
 *
 * Terminal condition
 *   all done[col] == 1  AND  empty_count == 0  → valid solution found.
 *
 * Pruning (applied before each recursive call)
 * ─────────────────────────────────────────────
 * 1. Per-color BFS reachability
 *      For every incomplete color, BFS from its front through empty cells.
 *      If the goal is unreachable, prune.
 *
 * 2. Global connectivity
 *      Flood-fill from all incomplete fronts + goals through empty cells.
 *      If any empty cell is NOT reached, it can never be filled → prune.
 *
 * 3. Dead-end detection
 *      If any incomplete front has zero valid moves AND is not adjacent
 *      to its goal, prune immediately (caught by pruning layer 1).
 *
 * Node limit
 *   A global node counter caps the search at MAX_NODES to prevent
 *   excessive runtime on very hard or malformed puzzles.
 */

#include "puzzle.h"
#include <string.h>

/* ── Constants ─────────────────────────────────────────────────────────── */
#define MAX_NODES 5000000L   /* search node budget                          */

static const int DR[4] = { -1,  1,  0,  0 };
static const int DC[4] = {  0,  0, -1,  1 };

/* ── Solver state ──────────────────────────────────────────────────────── */
typedef struct {
    int filled[MAX_ROWS][MAX_COLS]; /* 0 = empty; color = occupied           */
    int done   [MAX_COLORS + 1];   /* path complete?                         */
    int front_r[MAX_COLORS + 1];   /* active front position                  */
    int front_c[MAX_COLORS + 1];
    int goal_r [MAX_COLORS + 1];   /* fixed goal position                    */
    int goal_c [MAX_COLORS + 1];
    int rows;
    int cols;
    int num_colors;
    int empty_count;               /* cells not yet occupied                 */
} State;

/* ── Module-level result storage (single-threaded) ─────────────────────── */
static int  g_nsols;
static int  g_max_sols;
static int  g_solution[MAX_ROWS][MAX_COLS];
static long g_node_count;

/*
 * Node budget for the has_unfilled_solution() check.
 */
#define UNFILLED_NODE_BUDGET 30000L

/* ── Pruning helpers ───────────────────────────────────────────────────── */

/*
 * can_reach()
 *
 * BFS from the front of color col to its goal, traversing only empty
 * cells (or landing on the goal itself).
 * Returns 1 if reachable, 0 otherwise.
 */
static int can_reach(const State *s, int col)
{
    int fr = s->front_r[col], fc = s->front_c[col];
    int gr = s->goal_r [col], gc = s->goal_c [col];

    int qr[MAX_ROWS * MAX_COLS], qc[MAX_ROWS * MAX_COLS];
    int vis[MAX_ROWS][MAX_COLS];
    memset(vis, 0, sizeof(vis));

    int head = 0, tail = 0;
    vis[fr][fc] = 1;
    qr[tail] = fr; qc[tail] = fc; tail++;

    while (head < tail) {
        int cr = qr[head], cc = qc[head]; head++;
        for (int d = 0; d < 4; d++) {
            int nr = cr + DR[d], nc = cc + DC[d];
            if (nr < 0 || nr >= s->rows || nc < 0 || nc >= s->cols) continue;
            if (vis[nr][nc]) continue;
            if (nr == gr && nc == gc) return 1;
            if (s->filled[nr][nc] == 0) {
                vis[nr][nc] = 1;
                qr[tail] = nr; qc[tail] = nc; tail++;
            }
        }
    }
    return 0;
}

/*
 * connectivity_ok()
 *
 * Flood-fill from every incomplete path's front AND goal through empty
 * cells.  If any empty cell is not reached, it will never be filled.
 * Returns 1 if all empty cells are reachable, 0 otherwise.
 */
static int connectivity_ok(const State *s)
{
    if (s->empty_count == 0) return 1;

    int vis[MAX_ROWS][MAX_COLS];
    memset(vis, 0, sizeof(vis));
    int qr[MAX_ROWS * MAX_COLS], qc[MAX_ROWS * MAX_COLS];
    int head = 0, tail = 0;

    for (int col = 1; col <= s->num_colors; col++) {
        if (s->done[col]) continue;

        int fr = s->front_r[col], fc = s->front_c[col];
        int gr = s->goal_r[col],  gc = s->goal_c[col];

        if (!vis[fr][fc]) {
            vis[fr][fc] = 1; qr[tail] = fr; qc[tail] = fc; tail++;
        }
        if (!vis[gr][gc]) {
            vis[gr][gc] = 1; qr[tail] = gr; qc[tail] = gc; tail++;
        }
    }

    while (head < tail) {
        int cr = qr[head], cc = qc[head]; head++;
        for (int d = 0; d < 4; d++) {
            int nr = cr + DR[d], nc = cc + DC[d];
            if (nr < 0 || nr >= s->rows || nc < 0 || nc >= s->cols) continue;
            if (vis[nr][nc]) continue;
            if (s->filled[nr][nc] == 0) {
                vis[nr][nc] = 1;
                qr[tail] = nr; qc[tail] = nc; tail++;
            }
        }
    }

    for (int r = 0; r < s->rows; r++)
        for (int c = 0; c < s->cols; c++)
            if (s->filled[r][c] == 0 && !vis[r][c])
                return 0;
    return 1;
}

/*
 * would_create_2x2()
 *
 * Returns 1 if placing color col at (r,c) would complete any 2×2 block
 * whose other three cells are already filled with the same color.
 */
static int would_create_2x2(const State *s, int r, int c, int col)
{
    int tops[4][2] = { {r, c}, {r, c-1}, {r-1, c}, {r-1, c-1} };

    for (int i = 0; i < 4; i++) {
        int r0 = tops[i][0], c0 = tops[i][1];
        if (r0 < 0 || r0 + 1 >= s->rows) continue;
        if (c0 < 0 || c0 + 1 >= s->cols) continue;

        int all_same = 1;
        for (int dr = 0; dr <= 1 && all_same; dr++) {
            for (int dc = 0; dc <= 1 && all_same; dc++) {
                int nr = r0 + dr, nc = c0 + dc;
                if (nr == r && nc == c) continue;
                if (s->filled[nr][nc] != col) all_same = 0;
            }
        }
        if (all_same) return 1;
    }
    return 0;
}

/*
 * is_feasible()
 *
 * Combined feasibility check: per-color reachability + global connectivity.
 */
static int is_feasible(const State *s)
{
    for (int col = 1; col <= s->num_colors; col++) {
        if (s->done[col]) continue;
        if (!can_reach(s, col)) return 0;
    }
    return connectivity_ok(s);
}

/* ── MRV color selector ────────────────────────────────────────────────── */

static int count_moves(const State *s, int col)
{
    int fr = s->front_r[col], fc = s->front_c[col];
    int gr = s->goal_r [col], gc = s->goal_c [col];
    int moves = 0;
    for (int d = 0; d < 4; d++) {
        int nr = fr + DR[d], nc = fc + DC[d];
        if (nr < 0 || nr >= s->rows || nc < 0 || nc >= s->cols) continue;
        if (s->filled[nr][nc] == 0 || (nr == gr && nc == gc))
            moves++;
    }
    return moves;
}

static int pick_color(const State *s)
{
    int best_col = -1;
    int best_m   = MAX_ROWS * MAX_COLS + 1;

    for (int col = 1; col <= s->num_colors; col++) {
        if (s->done[col]) continue;
        int m = count_moves(s, col);
        if (m < best_m) {
            best_m   = m;
            best_col = col;
            if (m == 0) break;
        }
    }
    return best_col;
}

/* ── Recursive backtracking ────────────────────────────────────────────── */

static void backtrack(State *s)
{
    if (g_nsols >= g_max_sols)      return;
    if (++g_node_count > MAX_NODES) return;

    {
        int all_done = 1;
        for (int col = 1; col <= s->num_colors; col++)
            if (!s->done[col]) { all_done = 0; break; }
        if (all_done && s->empty_count == 0) {
            g_nsols++;
            if (g_nsols == 1)
                memcpy(g_solution, s->filled, sizeof(s->filled));
            return;
        }
        if (all_done) return;
    }

    if (!is_feasible(s)) return;

    int col = pick_color(s);
    if (col < 0) return;

    int fr = s->front_r[col], fc = s->front_c[col];
    int gr = s->goal_r [col], gc = s->goal_c [col];

    for (int d = 0; d < 4; d++) {
        int nr = fr + DR[d], nc = fc + DC[d];
        if (nr < 0 || nr >= s->rows || nc < 0 || nc >= s->cols) continue;

        if (nr == gr && nc == gc) {
            s->done[col]    = 1;
            s->front_r[col] = nr;
            s->front_c[col] = nc;

            backtrack(s);

            s->done[col]    = 0;
            s->front_r[col] = fr;
            s->front_c[col] = fc;

        } else if (s->filled[nr][nc] == 0) {
            if (would_create_2x2(s, nr, nc, col)) continue;

            s->filled[nr][nc] = col;
            s->front_r[col]   = nr;
            s->front_c[col]   = nc;
            s->empty_count--;

            backtrack(s);

            s->filled[nr][nc] = 0;
            s->front_r[col]   = fr;
            s->front_c[col]   = fc;
            s->empty_count++;
        }
    }
}

/* ── Setup and launch ──────────────────────────────────────────────────── */

static int run_solver(const Puzzle *p, int max_sols)
{
    State s;
    memset(&s, 0, sizeof(s));
    s.rows       = p->rows;
    s.cols       = p->cols;
    s.num_colors = p->num_colors;

    int empty = 0;
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            s.filled[r][c] = p->grid[r][c];
            if (p->grid[r][c] == 0) empty++;
        }
    }
    s.empty_count = empty;

    int ep_found[MAX_COLORS + 1] = {0};
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            int col = p->grid[r][c];
            if (!col) continue;
            if (ep_found[col] == 0) {
                s.front_r[col] = r; s.front_c[col] = c;
                ep_found[col]++;
            } else if (ep_found[col] == 1) {
                s.goal_r[col]  = r; s.goal_c[col]  = c;
                ep_found[col]++;
            }
        }
    }

    g_nsols     = 0;
    g_max_sols  = max_sols;
    g_node_count = 0;

    backtrack(&s);
    return g_nsols;
}

/* ── Unfilled-solution detector ────────────────────────────────────────── */

static void backtrack_nofill(State *s)
{
    if (g_nsols >= g_max_sols)              return;
    if (++g_node_count > UNFILLED_NODE_BUDGET) return;

    int all_done = 1;
    for (int col = 1; col <= s->num_colors; col++)
        if (!s->done[col]) { all_done = 0; break; }

    if (all_done) {
        if (s->empty_count > 0) {
            g_nsols++;
            if (g_nsols == 1)
                memcpy(g_solution, s->filled, sizeof(s->filled));
        }
        return;
    }

    for (int col = 1; col <= s->num_colors; col++) {
        if (s->done[col]) continue;
        if (!can_reach(s, col)) return;
    }

    int col = pick_color(s);
    if (col < 0) return;

    int fr = s->front_r[col], fc = s->front_c[col];
    int gr = s->goal_r [col], gc = s->goal_c [col];

    for (int d = 0; d < 4; d++) {
        int nr = fr + DR[d], nc = fc + DC[d];
        if (nr < 0 || nr >= s->rows || nc < 0 || nc >= s->cols) continue;

        if (nr == gr && nc == gc) {
            s->done[col]    = 1;
            s->front_r[col] = nr;
            s->front_c[col] = nc;

            backtrack_nofill(s);

            s->done[col]    = 0;
            s->front_r[col] = fr;
            s->front_c[col] = fc;

        } else if (s->filled[nr][nc] == 0) {
            s->filled[nr][nc] = col;
            s->front_r[col]   = nr;
            s->front_c[col]   = nc;
            s->empty_count--;

            backtrack_nofill(s);

            s->filled[nr][nc] = 0;
            s->front_r[col]   = fr;
            s->front_c[col]   = fc;
            s->empty_count++;
        }
    }
}

/* ── Public interface ──────────────────────────────────────────────────── */

int solve_puzzle(const Puzzle *p, int out[MAX_ROWS][MAX_COLS])
{
    int n = run_solver(p, 1);
    if (n > 0) {
        memcpy(out, g_solution, sizeof(g_solution));
        return 1;
    }
    return 0;
}

int count_solutions(const Puzzle *p, int max_count)
{
    return run_solver(p, max_count);
}

int validate_puzzle(const Puzzle *p)
{
    if (!p || p->rows < 2 || p->rows > MAX_ROWS) return 0;
    if (p->cols < 2 || p->cols > MAX_COLS)       return 0;
    if (p->num_colors < 2 || p->num_colors > MAX_COLORS) return 0;

    int ep_count[MAX_COLORS + 1] = {0};
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            int col = p->grid[r][c];
            if (col < 0 || col > p->num_colors) return 0;
            if (col > 0) ep_count[col]++;
        }
    }
    for (int col = 1; col <= p->num_colors; col++)
        if (ep_count[col] != 2) return 0;

    return 1;
}

int has_unfilled_solution(const Puzzle *p)
{
    State s;
    memset(&s, 0, sizeof(s));
    s.rows       = p->rows;
    s.cols       = p->cols;
    s.num_colors = p->num_colors;

    int empty = 0;
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            s.filled[r][c] = p->grid[r][c];
            if (p->grid[r][c] == 0) empty++;
        }
    }
    s.empty_count = empty;

    int ep_found[MAX_COLORS + 1] = {0};
    for (int r = 0; r < p->rows; r++) {
        for (int c = 0; c < p->cols; c++) {
            int col = p->grid[r][c];
            if (!col) continue;
            if (ep_found[col] == 0) {
                s.front_r[col] = r; s.front_c[col] = c;
                ep_found[col]++;
            } else if (ep_found[col] == 1) {
                s.goal_r[col]  = r; s.goal_c[col]  = c;
                ep_found[col]++;
            }
        }
    }

    g_nsols      = 0;
    g_max_sols   = 1;
    g_node_count = 0;

    backtrack_nofill(&s);
    return g_nsols > 0;
}
