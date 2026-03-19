/*
 * main.c — Free Flow / Numberlink puzzle generator and validator
 *
 * Usage:  free_flow [-s SIZE] [-r ROWS] [-W COLS] [-c COLORS] [-N COUNT]
 *                   [-R] [-S SEED] [-u] [-n] [-h]
 *
 *   -s SIZE    Square grid (SIZE×SIZE), default 7
 *   -r ROWS    Number of rows (overrides -s)
 *   -W COLS    Number of columns (overrides -s)
 *   -c COLORS  Number of color pairs (2–14, default auto = min_dim-1)
 *   -N COUNT   Generate COUNT puzzles into one JSON file (default 1)
 *   -R         Randomize color count per puzzle: pick from {n, n-1, n-2}
 *   -S SEED    Random seed (default: time-based)
 *   -u         Require a unique solution (retry until found)
 *   -n         Disable ANSI colors (plain ASCII output)
 *   -h         Show this help
 *
 * Output (COUNT = 1)
 * ──────────────────
 * Prints the puzzle + solution to stdout and writes {rows}x{cols}.json.
 *
 * Output (COUNT > 1)
 * ──────────────────
 * Shows a progress line and writes {rows}x{cols}_x{COUNT}.json containing
 * a top-level metadata wrapper and a "puzzles" array of COUNT objects.
 * With -R each puzzle may have a different num_colors.
 *
 * Display conventions
 * ───────────────────
 * Colors 1–14 are mapped to ANSI foreground color codes.
 * Endpoint cells are shown as bold capital letters (R G Y B M C …).
 * Interior path cells are shown as a middle-dot '·' (or lowercase in plain mode).
 */

#include "puzzle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>   /* isatty() */

/* ── ANSI display ──────────────────────────────────────────────────────── */

static const char *ANSI_FG[MAX_COLORS + 1] = {
    "\033[0m",    /* 0  reset         */
    "\033[31m",   /* 1  red           */
    "\033[32m",   /* 2  green         */
    "\033[33m",   /* 3  yellow        */
    "\033[34m",   /* 4  blue          */
    "\033[35m",   /* 5  magenta       */
    "\033[36m",   /* 6  cyan          */
    "\033[91m",   /* 7  bright red    */
    "\033[92m",   /* 8  bright green  */
    "\033[93m",   /* 9  bright yellow */
    "\033[94m",   /* 10 bright blue   */
    "\033[95m",   /* 11 bright magenta*/
    "\033[96m",   /* 12 bright cyan   */
    "\033[37m",   /* 13 white         */
    "\033[90m",   /* 14 dark grey     */
};

#define ANSI_BOLD  "\033[1m"
#define ANSI_RESET "\033[0m"

/* Label characters — index 0 = empty, 1–14 = color letters */
static const char LABELS[] = ".RGYBMCOPATNVKX";

static int g_use_color = 1;

/* ── Cell printers ─────────────────────────────────────────────────────── */

static void print_endpoint(int color)
{
    if (g_use_color)
        printf("%s%s%c%s", ANSI_FG[color], ANSI_BOLD, LABELS[color], ANSI_RESET);
    else
        printf("%c", LABELS[color]);
}

static void print_path_cell(int color)
{
    if (g_use_color)
        printf("%s\xc2\xb7%s", ANSI_FG[color], ANSI_RESET);
    else
        printf("%c", LABELS[color] + ('a' - 'A'));
}

static void print_empty(void) { printf("."); }

/* ── Grid border helpers ────────────────────────────────────────────────── */

static void print_hline(int cols)
{
    printf("  +");
    for (int c = 0; c < cols; c++) printf("--");
    printf("-+\n");
}

static void print_col_numbers(int cols)
{
    printf("   ");
    for (int c = 0; c < cols; c++) printf(" %d", c % 10);
    printf("\n");
}

/* ── Public display functions ──────────────────────────────────────────── */

void print_puzzle(const Puzzle *p)
{
    printf("Puzzle (%dx%d, %d color%s):\n",
           p->rows, p->cols, p->num_colors, p->num_colors == 1 ? "" : "s");
    print_hline(p->cols);
    for (int r = 0; r < p->rows; r++) {
        printf("%d |", r % 10);
        for (int c = 0; c < p->cols; c++) {
            int col = p->grid[r][c];
            printf(" ");
            if (col > 0) print_endpoint(col);
            else         print_empty();
        }
        printf(" |\n");
    }
    print_hline(p->cols);
    print_col_numbers(p->cols);
    printf("\n");
}

void print_solution(const Puzzle *p, const int sol[MAX_ROWS][MAX_COLS])
{
    printf("Solution:\n");
    print_hline(p->cols);
    for (int r = 0; r < p->rows; r++) {
        printf("%d |", r % 10);
        for (int c = 0; c < p->cols; c++) {
            int col = sol[r][c];
            printf(" ");
            if (p->grid[r][c] > 0)  print_endpoint(col);
            else                     print_path_cell(col);
        }
        printf(" |\n");
    }
    print_hline(p->cols);
    print_col_numbers(p->cols);
    printf("\n");
}

/* ── Color key ─────────────────────────────────────────────────────────── */

static void print_color_key(int num_colors)
{
    printf("Color key: ");
    for (int col = 1; col <= num_colors; col++) {
        if (g_use_color)
            printf("%s%s%c%s", ANSI_FG[col], ANSI_BOLD, LABELS[col], ANSI_RESET);
        else
            printf("%c", LABELS[col]);
        if (col < num_colors) printf(" ");
    }
    printf("\n\n");
}

/* ── JSON helpers ──────────────────────────────────────────────────────── */

/*
 * fnv1a32_grid()
 *
 * FNV-1a 32-bit hash over only the used rows×cols cells.
 * Hashing the full MAX_ROWS×MAX_COLS array would waste ~37% on a 10×14
 * puzzle (padding zeros are deterministic but add unnecessary work).
 */
static uint32_t fnv1a32_grid(const int grid[MAX_ROWS][MAX_COLS],
                               int rows, int cols)
{
    uint32_t h = 2166136261u;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const unsigned char *b = (const unsigned char *)&grid[r][c];
            for (int i = 0; i < (int)sizeof(int); i++) {
                h ^= b[i];
                h *= 16777619u;
            }
        }
    }
    return h;
}

/*
 * difficulty_label()
 *
 * Classify based on average path length (total cells / num_colors).
 * Longer paths demand more look-ahead and are harder for human solvers.
 *
 *   avg < 6   → easy
 *   avg < 12  → medium
 *   avg < 20  → hard
 *   avg ≥ 20  → expert
 */
static const char *difficulty_label(int rows, int cols, int num_colors)
{
    double avg = (double)(rows * cols) / num_colors;
    if (avg < 6.0)  return "easy";
    if (avg < 12.0) return "medium";
    if (avg < 20.0) return "hard";
    return "expert";
}

/*
 * write_grid_json()
 *
 * Write a 2-D integer grid as a named JSON array field.
 * Used for both "puzzle" and "solution" to avoid code duplication.
 * trailing_comma controls whether a comma follows the closing bracket.
 */
static void write_grid_json(FILE *f, const int grid[MAX_ROWS][MAX_COLS],
                             int rows, int cols,
                             const char *field, const char *ind,
                             int trailing_comma)
{
    fprintf(f, "%s  \"%s\": [\n", ind, field);
    for (int r = 0; r < rows; r++) {
        fprintf(f, "%s    [", ind);
        for (int c = 0; c < cols; c++)
            fprintf(f, "%d%s", grid[r][c], c < cols - 1 ? ", " : "");
        fprintf(f, "]%s\n", r < rows - 1 ? "," : "");
    }
    fprintf(f, "%s  ]%s\n", ind, trailing_comma ? "," : "");
}

/* ── Core JSON writer ──────────────────────────────────────────────────── */

/*
 * write_puzzle_object()
 *
 * Write one puzzle as a JSON object to the already-open file f.
 *
 *   ind      — indent prefix for each line (e.g. "    " inside an array)
 *   index    — 0-based batch position; pass -1 for single-puzzle mode,
 *              which emits rows/cols/seed instead of an "index" field
 *   seed     — original srand() seed (single mode only)
 *
 * num_colors is always emitted from p->num_colors so each object is
 * self-contained even when -R produces varying counts across a batch.
 *
 * The object is written without a trailing newline or comma.
 */
static void write_puzzle_object(FILE *f, const Puzzle *p,
                                 unsigned int seed, int index,
                                 const char *ind)
{
    uint32_t h1   = fnv1a32_grid(p->grid,     p->rows, p->cols);
    uint32_t h2   = fnv1a32_grid(p->solution, p->rows, p->cols);
    uint32_t hash = h1 ^ (h2 * 2654435761u);

    double avg_path = (double)(p->rows * p->cols) / p->num_colors;

    /* Path length per color */
    int path_len[MAX_COLORS + 1];
    memset(path_len, 0, sizeof(path_len));
    for (int r = 0; r < p->rows; r++)
        for (int c = 0; c < p->cols; c++)
            if (p->solution[r][c] > 0)
                path_len[p->solution[r][c]]++;

    fprintf(f, "%s{\n", ind);

    if (index < 0) {
        /* Single-puzzle mode: embed shared metadata inside the object */
        fprintf(f, "%s  \"rows\": %d,\n",       ind, p->rows);
        fprintf(f, "%s  \"cols\": %d,\n",       ind, p->cols);
        fprintf(f, "%s  \"seed\": %u,\n",       ind, seed);
    } else {
        fprintf(f, "%s  \"index\": %d,\n", ind, index);
    }

    /* num_colors always present — each object is self-contained */
    fprintf(f, "%s  \"num_colors\": %d,\n",  ind, p->num_colors);
    fprintf(f, "%s  \"difficulty\": \"%s\",\n", ind,
            difficulty_label(p->rows, p->cols, p->num_colors));
    fprintf(f, "%s  \"avg_path_length\": %.2f,\n", ind, avg_path);
    fprintf(f, "%s  \"hash\": \"%08x\",\n",  ind, hash);

    /* colors */
    fprintf(f, "%s  \"colors\": [\n", ind);
    for (int col = 1; col <= p->num_colors; col++) {
        int sr = -1, sc = -1, er = -1, ec = -1, found = 0;
        for (int r = 0; r < p->rows && found < 2; r++)
            for (int c = 0; c < p->cols && found < 2; c++)
                if (p->grid[r][c] == col) {
                    if (!found) { sr = r; sc = c; }
                    else        { er = r; ec = c; }
                    found++;
                }
        fprintf(f,
            "%s    {\"id\": %d, \"label\": \"%c\", "
            "\"path_length\": %d, "
            "\"start\": [%d, %d], \"end\": [%d, %d]}%s\n",
            ind, col, LABELS[col], path_len[col],
            sr, sc, er, ec, col < p->num_colors ? "," : "");
    }
    fprintf(f, "%s  ],\n", ind);

    write_grid_json(f, p->grid,     p->rows, p->cols, "puzzle",   ind, 1);
    write_grid_json(f, p->solution, p->rows, p->cols, "solution", ind, 0);

    fprintf(f, "%s}", ind);
}

/* ── Usage ─────────────────────────────────────────────────────────────── */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [-s SIZE] [-r ROWS] [-W COLS] [-c COLORS] [-N COUNT]\n"
        "       %*s [-R] [-S SEED] [-u] [-n] [-C] [-h]\n"
        "\n"
        "  -s SIZE    Square grid SIZE×SIZE (2-%d, default 7)\n"
        "  -r ROWS    Number of rows (2-%d, overrides -s)\n"
        "  -W COLS    Number of columns (2-%d, overrides -s)\n"
        "  -c COLORS  Number of color pairs (2-%d, default auto = min_dim-1)\n"
        "  -N COUNT   Generate COUNT puzzles into one JSON file (default 1)\n"
        "  -R         Randomize colors per puzzle: pick from {n, n-1, n-2}\n"
        "  -S SEED    Random seed (default: time-based)\n"
        "  -u         Require unique solution (retry until found)\n"
        "  -n         Disable ANSI colors (plain ASCII)\n"
        "  -C         Force ANSI colors even when stdout is not a TTY\n"
        "  -h         Show this help\n"
        "\n"
        "Output files\n"
        "  COUNT=1  → {rows}x{cols}.json           (single puzzle object)\n"
        "  COUNT>1  → {rows}x{cols}_x{COUNT}.json  (array of COUNT puzzles)\n"
        "\n"
        "Rules (Numberlink / Flow Free):\n"
        "  Connect each pair of matching colored dots with a path.\n"
        "  Paths may not cross. Every cell must be filled.\n",
        prog, (int)strlen(prog) + 7, "",
        MAX_ROWS, MAX_ROWS, MAX_COLS, MAX_COLORS);
}

/* ── Puzzle generation (one valid puzzle) ──────────────────────────────── */

/*
 * gen_valid_puzzle()
 *
 * Run the generate+validate loop until a valid puzzle is produced.
 * Returns: number of attempts on success (>0), 0 on fatal generator
 * error, -1 if the 50 000-attempt limit is exhausted.
 */
static int gen_valid_puzzle(Puzzle *p, int rows, int cols,
                             int num_colors, int require_unique)
{
    for (int attempt = 1; attempt <= 50000; attempt++) {
        /*
         * generate_puzzle() has its own internal retry budget.  When it
         * returns 0 the rand() state has advanced, so a subsequent call
         * will explore a different part of the search space — don't bail.
         * Truly invalid parameters are caught by explicit checks in main().
         */
        if (!generate_puzzle(p, rows, cols, num_colors)) continue;

        if (!validate_puzzle(p)) continue;

        if (require_unique && count_solutions(p, 2) >= 2) continue;

        if (has_unfilled_solution(p)) continue;

        return attempt;
    }
    return -1;
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    int  rows           = 7;
    int  cols           = 7;
    int  num_colors     = -1;   /* -1 = auto-compute */
    int  num_puzzles    = 1;
    int  rand_colors    = 0;    /* -R: randomize color count per puzzle */
    int  require_unique = 0;
    unsigned int seed   = (unsigned int)time(NULL);

    /* ── Parse arguments ─────────────────────────────────────────────── */
    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "-s") == 0 && i+1 < argc) rows = cols = atoi(argv[++i]);
        else if (strcmp(argv[i], "-r") == 0 && i+1 < argc) rows        = atoi(argv[++i]);
        else if (strcmp(argv[i], "-W") == 0 && i+1 < argc) cols        = atoi(argv[++i]);
        else if (strcmp(argv[i], "-c") == 0 && i+1 < argc) num_colors  = atoi(argv[++i]);
        else if (strcmp(argv[i], "-N") == 0 && i+1 < argc) num_puzzles = atoi(argv[++i]);
        else if (strcmp(argv[i], "-S") == 0 && i+1 < argc) seed = (unsigned int)atoi(argv[++i]);
        else if (strcmp(argv[i], "-u") == 0) require_unique = 1;
        else if (strcmp(argv[i], "-R") == 0) rand_colors    = 1;
        else if (strcmp(argv[i], "-n") == 0) g_use_color    = 0;
        else if (strcmp(argv[i], "-C") == 0) g_use_color    = 2;
        else if (strcmp(argv[i], "-h") == 0) { usage(argv[0]); return 0; }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (g_use_color == 1 && !isatty(STDOUT_FILENO))
        g_use_color = 0;

    /* Auto-compute base color count: smaller dimension - 1 */
    if (num_colors < 0) {
        int min_dim = rows < cols ? rows : cols;
        num_colors = min_dim - 1;
        if (num_colors > MAX_COLORS) num_colors = MAX_COLORS;
        if (num_colors < 2)          num_colors = 2;
    }
    int base_colors = num_colors;   /* save for -R randomization */

    /* Validate */
    if (rows < 2 || rows > MAX_ROWS) { fprintf(stderr, "Error: rows must be 2-%d\n",   MAX_ROWS);   return 1; }
    if (cols < 2 || cols > MAX_COLS) { fprintf(stderr, "Error: cols must be 2-%d\n",   MAX_COLS);   return 1; }
    if (num_colors < 2 || num_colors > MAX_COLORS) { fprintf(stderr, "Error: colors must be 2-%d\n", MAX_COLORS); return 1; }
    if (num_colors * 2 > rows * cols) {
        fprintf(stderr, "Error: %d colors need %d cells but %dx%d only has %d\n",
                num_colors, num_colors * 2, rows, cols, rows * cols);
        return 1;
    }
    if (num_puzzles < 1) { fprintf(stderr, "Error: -N must be >= 1\n"); return 1; }

    srand(seed);

    printf("Free Flow / Numberlink Generator\n");
    printf("═════════════════════════════════\n");
    if (rand_colors)
        printf("Seed: %u  |  Grid: %dx%d  |  Colors: %d-%d (random)  |  Count: %d\n\n",
               seed, rows, cols,
               (base_colors - 2 < 2 ? 2 : base_colors - 2), base_colors,
               num_puzzles);
    else
        printf("Seed: %u  |  Grid: %dx%d  |  Colors: %d  |  Count: %d\n\n",
               seed, rows, cols, num_colors, num_puzzles);

    /* ── Single puzzle ───────────────────────────────────────────────── */
    if (num_puzzles == 1) {
        /* With -R, pick a random color count for this one puzzle */
        if (rand_colors) {
            num_colors = base_colors - (rand() % 3);
            if (num_colors < 2) num_colors = 2;
        }

        Puzzle p;
        int attempts = gen_valid_puzzle(&p, rows, cols, num_colors, require_unique);
        if (attempts < 0) {
            fprintf(stderr, "Error: could not find a valid puzzle in 50000 attempts.\n"
                            "Tip: try a smaller grid or fewer colors.\n");
            return 1;
        }
        if (attempts > 1)
            printf("Generated after %d attempts.\n\n", attempts);

        print_color_key(num_colors);
        print_puzzle(&p);
        print_solution(&p, p.solution);

        if (require_unique) {
            printf("✓ Unique solution — well-formed Numberlink puzzle.\n");
        } else {
            int nsols = (rows * cols <= 81) ? count_solutions(&p, 2) : 1;
            if (nsols == 1)
                printf("✓ Unique solution — well-formed Numberlink puzzle.\n");
            else {
                printf("⚠ Multiple solutions exist (≥2).\n");
                printf("  Use -u to enforce uniqueness at generation time.\n");
            }
        }

        char filename[64];
        snprintf(filename, sizeof(filename), "%dx%d.json", rows, cols);
        FILE *f = fopen(filename, "w");
        if (!f) { fprintf(stderr, "Warning: could not write %s\n", filename); return 0; }
        write_puzzle_object(f, &p, seed, -1, "");
        fprintf(f, "\n");
        fclose(f);
        printf("JSON written to %s\n", filename);
        return 0;
    }

    /* ── Batch mode (num_puzzles > 1) ────────────────────────────────── */
    char filename[64];
    snprintf(filename, sizeof(filename), "%dx%d_x%d.json", rows, cols, num_puzzles);

    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Error: could not open %s for writing\n", filename);
        return 1;
    }

    /* Top-level wrapper */
    fprintf(f, "{\n");
    fprintf(f, "  \"rows\": %d,\n",    rows);
    fprintf(f, "  \"cols\": %d,\n",    cols);
    fprintf(f, "  \"count\": %d,\n",   num_puzzles);
    fprintf(f, "  \"seed\": %u,\n",    seed);
    if (rand_colors) {
        int lo = base_colors - 2 < 2 ? 2 : base_colors - 2;
        fprintf(f, "  \"num_colors_range\": [%d, %d],\n", lo, base_colors);
    } else {
        fprintf(f, "  \"num_colors\": %d,\n", num_colors);
    }
    fprintf(f, "  \"puzzles\": [\n");

    int total_attempts = 0;
    for (int i = 0; i < num_puzzles; i++) {
        /* -R: pick color count randomly from {base, base-1, base-2} */
        int nc = base_colors;
        if (rand_colors) {
            nc = base_colors - (rand() % 3);
            if (nc < 2) nc = 2;
        }

        Puzzle p;
        int attempts = gen_valid_puzzle(&p, rows, cols, nc, require_unique);
        if (attempts < 0) {
            fprintf(stderr, "\nError: attempt limit reached on puzzle %d/%d.\n"
                            "Tip: try fewer colors or a smaller grid.\n",
                    i + 1, num_puzzles);
            fclose(f);
            remove(filename);   /* don't leave a partial/invalid JSON file */
            return 1;
        }
        total_attempts += attempts;

        write_puzzle_object(f, &p, seed, i, "    ");
        fprintf(f, "%s\n", i < num_puzzles - 1 ? "," : "");

        printf("\r  [%d/%d] generated  (total attempts: %d)   ",
               i + 1, num_puzzles, total_attempts);
        fflush(stdout);
    }

    fprintf(f, "  ]\n}\n");
    fclose(f);

    printf("\nJSON written to %s\n", filename);
    return 0;
}
