/*
 * puzzle.h — Free Flow / Numberlink puzzle data structures and interface
 *
 * A Numberlink (Flow Free) puzzle is an n×n grid containing k pairs of
 * colored endpoints.  The solver must connect each pair with a continuous
 * non-branching path such that:
 *   • No two paths cross or share a cell.
 *   • Every cell in the grid is occupied by exactly one path.
 *
 * GENERATION (NP-complete)
 * ────────────────────────
 * We use a "solution-first / growing snakes" approach:
 *   1. Place k random seed cells (one per color).
 *   2. Randomly extend the tail of any extendable path into an adjacent
 *      empty cell until the entire grid is covered.
 *   3. The head and tail of each path become the puzzle's endpoint clues.
 * If the process gets stuck or produces paths of length < 2, we retry
 * with a different random state (up to MAX_GEN_ATTEMPTS times).
 *
 * SOLVING / VALIDATION
 * ─────────────────────
 * Backtracking search with two pruning layers:
 *   • MRV heuristic   – always extend the color with fewest valid moves.
 *   • BFS feasibility – per color: can the active front still reach its
 *                       goal through empty cells?
 *   • Connectivity    – are ALL remaining empty cells reachable from at
 *                       least one incomplete path front?
 */

#ifndef PUZZLE_H
#define PUZZLE_H

/* ── Compile-time limits ───────────────────────────────────────────────── */
#define MAX_SIZE    12   /* maximum grid dimension (12×12 = 144 cells)      */
#define MAX_COLORS  14   /* maximum number of color pairs                   */

/* ── Core data structure ───────────────────────────────────────────────── */
typedef struct {
    int size;        /* grid dimension (size × size)                        */
    int num_colors;  /* number of color pairs                               */

    /*
     * grid[r][c]:
     *   0          → empty cell (no clue placed here)
     *   1..num_colors → colored endpoint clue
     * Only the two endpoints per color appear here; interior path cells
     * are 0 in the puzzle but filled in the solution.
     */
    int grid[MAX_SIZE][MAX_SIZE];

    /*
     * solution[r][c]:
     *   The color (1..num_colors) that occupies each cell in the known
     *   solution produced by the generator.  The solver may find a
     *   different (equally valid) solution.
     */
    int solution[MAX_SIZE][MAX_SIZE];
} Puzzle;

/* ── Generator ─────────────────────────────────────────────────────────── */

/*
 * generate_puzzle()
 *
 * Fill *p with a randomly generated Numberlink puzzle of the given size
 * and number of colors.  Uses the growing-snakes algorithm with retries.
 *
 * Returns 1 on success, 0 if no valid puzzle could be produced (e.g.,
 * num_colors is too large for the grid).
 *
 * Preconditions:
 *   2 ≤ size       ≤ MAX_SIZE
 *   2 ≤ num_colors ≤ MAX_COLORS
 *   num_colors * 2 ≤ size * size
 */
int generate_puzzle(Puzzle *p, int size, int num_colors);

/* ── Solver ────────────────────────────────────────────────────────────── */

/*
 * solve_puzzle()
 *
 * Find one solution to puzzle *p using backtracking + BFS pruning.
 * On success, out[r][c] contains the color (1..num_colors) of each cell.
 *
 * Returns 1 if a solution was found, 0 if the puzzle is unsolvable.
 */
int solve_puzzle(const Puzzle *p, int out[MAX_SIZE][MAX_SIZE]);

/*
 * count_solutions()
 *
 * Count distinct solutions up to max_count.  Useful for checking whether
 * a puzzle has a unique solution (call with max_count = 2).
 *
 * Returns the number of solutions found (capped at max_count).
 */
int count_solutions(const Puzzle *p, int max_count);

/*
 * has_unfilled_solution()
 *
 * Returns 1 if there exists any set of non-crossing paths that connects
 * every pair of endpoints while leaving at least one cell empty.
 *
 * A valid Numberlink puzzle must return 0: the topology of the endpoint
 * placement should make it impossible to connect all pairs without also
 * covering every cell.  If this returns 1, the puzzle is topologically
 * degenerate — some pairs can be "shortcut" without using all the cells,
 * meaning the fill-all-cells rule is doing all the work rather than the
 * puzzle design itself.
 */
int has_unfilled_solution(const Puzzle *p);

/*
 * validate_puzzle()
 *
 * Structural sanity check (does NOT run the solver):
 *   • Each color appears exactly twice in grid[][].
 *   • No color index is out of range.
 *
 * Returns 1 if valid, 0 otherwise.
 */
int validate_puzzle(const Puzzle *p);

/* ── Display ───────────────────────────────────────────────────────────── */

/*
 * print_puzzle()
 *
 * Print the unsolved puzzle grid (endpoints only) to stdout.
 * ANSI color codes are used when stdout is a TTY.
 */
void print_puzzle(const Puzzle *p);

/*
 * print_solution()
 *
 * Print a fully colored solution grid to stdout.
 * Endpoint cells are shown as bold letters; path cells as '·'.
 */
void print_solution(const Puzzle *p, const int sol[MAX_SIZE][MAX_SIZE]);

#endif /* PUZZLE_H */
