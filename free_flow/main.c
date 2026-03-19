/*
 * main.c — Free Flow / Numberlink puzzle generator and validator
 *
 * Usage:  free_flow [-s SIZE] [-r ROWS] [-W COLS] [-c COLORS] [-S SEED] [-u] [-n]
 *
 *   -s SIZE    Square grid (SIZE×SIZE), default 7
 *   -r ROWS    Number of rows (overrides -s)
 *   -W COLS    Number of columns (overrides -s)
 *   -c COLORS  Number of color pairs (2–14, default auto)
 *   -S SEED    Random seed (default: time-based)
 *   -u         Require a unique solution (retry until found)
 *   -n         Disable ANSI colors (plain ASCII output)
 *   -h         Show this help
 *
 * Output
 * ──────
 * 1. The unsolved puzzle grid (colored endpoints, empty cells as '.')
 * 2. The solved grid (full colored paths)
 * 3. A uniqueness verdict
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

/*
 * 14 visually distinct foreground colors (matches MAX_COLORS).
 * Index 0 is the reset code; indices 1–14 correspond to colors 1–14.
 */
static const char *ANSI_FG[MAX_COLORS + 1] = {
    "\033[0m",    /* 0  reset  */
    "\033[31m",   /* 1  red    */
    "\033[32m",   /* 2  green  */
    "\033[33m",   /* 3  yellow */
    "\033[34m",   /* 4  blue   */
    "\033[35m",   /* 5  magenta*/
    "\033[36m",   /* 6  cyan   */
    "\033[91m",   /* 7  bright red    */
    "\033[92m",   /* 8  bright green  */
    "\033[93m",   /* 9  bright yellow */
    "\033[94m",   /* 10 bright blue   */
    "\033[95m",   /* 11 bright magenta*/
    "\033[96m",   /* 12 bright cyan   */
    "\033[37m",   /* 13 white  */
    "\033[90m",   /* 14 dark grey */
};

#define ANSI_BOLD  "\033[1m"
#define ANSI_RESET "\033[0m"

/*
 * Single-character label for each color (index 0 = empty cell).
 * All 14 letters are visually distinct — no repeats.
 */
static const char LABELS[] = ".RGYBMCOPATNVKX";
/*                             0123456789ABCDE  (index in the array) */

static int g_use_color = 1; /* controlled by -n flag */

/* ── Cell printers ─────────────────────────────────────────────────────── */

/* Print a colored endpoint (bold letter) */
static void print_endpoint(int color)
{
    if (g_use_color)
        printf("%s%s%c%s", ANSI_FG[color], ANSI_BOLD, LABELS[color], ANSI_RESET);
    else
        printf("%c", LABELS[color]);
}

/* Print a colored path cell (non-bold middle-dot or lowercase letter) */
static void print_path_cell(int color)
{
    if (g_use_color)
        printf("%s\xc2\xb7%s", ANSI_FG[color], ANSI_RESET); /* UTF-8 '·' */
    else
        printf("%c", LABELS[color] + ('a' - 'A')); /* lowercase letter */
}

/* Print an empty cell */
static void print_empty(void)
{
    printf(".");
}

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

/* ── JSON output ───────────────────────────────────────────────────────── */

/*
 * fnv1a32()
 *
 * FNV-1a 32-bit hash — fast, deterministic, no dependencies.
 * Used to produce a unique fingerprint for each puzzle.
 */
static uint32_t fnv1a32(const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/*
 * difficulty_label()
 *
 * Classify puzzle difficulty based on average path length
 * (total cells / number of colors).  Longer paths require more
 * look-ahead and are generally harder for human solvers.
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
 * write_puzzle_json()
 *
 * Write the puzzle to a JSON file named "{rows}x{cols}.json".
 *
 * Fields:
 *   rows, cols, num_colors, seed
 *   difficulty        — "easy" / "medium" / "hard" / "expert"
 *   avg_path_length   — cells ÷ colors (difficulty proxy)
 *   hash              — FNV-1a fingerprint of puzzle + solution grids
 *   colors[]          — per-color metadata (label, endpoints, path length)
 *   puzzle[][]        — sparse grid: 0 = empty, 1..k = endpoint clue
 *   solution[][]      — fully connected: every cell has a color 1..k
 */
static void write_puzzle_json(const Puzzle *p, unsigned int seed)
{
    char filename[64];
    snprintf(filename, sizeof(filename), "%dx%d.json", p->rows, p->cols);

    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Warning: could not write %s\n", filename);
        return;
    }

    /* Hash: XOR-combine fingerprints of both grids for a stable unique id */
    uint32_t h1   = fnv1a32(p->grid,     sizeof(p->grid));
    uint32_t h2   = fnv1a32(p->solution, sizeof(p->solution));
    uint32_t hash = h1 ^ (h2 * 2654435761u);   /* Knuth multiplicative mix */

    double avg_path = (double)(p->rows * p->cols) / p->num_colors;

    /* Count path length per color from the solution grid */
    int path_len[MAX_COLORS + 1];
    memset(path_len, 0, sizeof(path_len));
    for (int r = 0; r < p->rows; r++)
        for (int c = 0; c < p->cols; c++)
            if (p->solution[r][c] > 0)
                path_len[p->solution[r][c]]++;

    fprintf(f, "{\n");
    fprintf(f, "  \"rows\": %d,\n",        p->rows);
    fprintf(f, "  \"cols\": %d,\n",        p->cols);
    fprintf(f, "  \"num_colors\": %d,\n",  p->num_colors);
    fprintf(f, "  \"seed\": %u,\n",        seed);
    fprintf(f, "  \"difficulty\": \"%s\",\n",
            difficulty_label(p->rows, p->cols, p->num_colors));
    fprintf(f, "  \"avg_path_length\": %.2f,\n", avg_path);
    fprintf(f, "  \"hash\": \"%08x\",\n",  hash);

    /* ── colors ── */
    fprintf(f, "  \"colors\": [\n");
    for (int col = 1; col <= p->num_colors; col++) {
        /* Find the two endpoints in row-major order */
        int sr = -1, sc = -1, er = -1, ec = -1, found = 0;
        for (int r = 0; r < p->rows && found < 2; r++) {
            for (int c = 0; c < p->cols && found < 2; c++) {
                if (p->grid[r][c] == col) {
                    if (!found) { sr = r; sc = c; }
                    else        { er = r; ec = c; }
                    found++;
                }
            }
        }
        fprintf(f,
            "    {\"id\": %d, \"label\": \"%c\", "
            "\"path_length\": %d, "
            "\"start\": [%d, %d], \"end\": [%d, %d]}%s\n",
            col, LABELS[col], path_len[col],
            sr, sc, er, ec,
            col < p->num_colors ? "," : "");
    }
    fprintf(f, "  ],\n");

    /* ── puzzle (sparse endpoint grid) ── */
    fprintf(f, "  \"puzzle\": [\n");
    for (int r = 0; r < p->rows; r++) {
        fprintf(f, "    [");
        for (int c = 0; c < p->cols; c++)
            fprintf(f, "%d%s", p->grid[r][c], c < p->cols - 1 ? ", " : "");
        fprintf(f, "]%s\n", r < p->rows - 1 ? "," : "");
    }
    fprintf(f, "  ],\n");

    /* ── solution (fully connected) ── */
    fprintf(f, "  \"solution\": [\n");
    for (int r = 0; r < p->rows; r++) {
        fprintf(f, "    [");
        for (int c = 0; c < p->cols; c++)
            fprintf(f, "%d%s", p->solution[r][c], c < p->cols - 1 ? ", " : "");
        fprintf(f, "]%s\n", r < p->rows - 1 ? "," : "");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);
    printf("JSON written to %s\n", filename);
}

/* ── Usage ─────────────────────────────────────────────────────────────── */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [-s SIZE] [-r ROWS] [-W COLS] [-c COLORS] [-S SEED] [-u] [-n] [-h]\n"
        "\n"
        "  -s SIZE    Square grid SIZE×SIZE (2-%d, default 7)\n"
        "  -r ROWS    Number of rows (2-%d, overrides -s)\n"
        "  -W COLS    Number of columns (2-%d, overrides -s)\n"
        "  -c COLORS  Number of color pairs (2-%d, default auto)\n"
        "  -S SEED    Random seed (default: time-based)\n"
        "  -u         Require unique solution (retry until found)\n"
        "  -n         Disable ANSI colors (plain ASCII)\n"
        "  -C         Force ANSI colors even when stdout is not a TTY\n"
        "  -h         Show this help\n"
        "\n"
        "Rules (Numberlink / Flow Free):\n"
        "  Connect each pair of matching colored dots with a path.\n"
        "  Paths may not cross. Every cell must be filled.\n",
        prog, MAX_ROWS, MAX_ROWS, MAX_COLS, MAX_COLORS);
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    int  rows           = 7;
    int  cols           = 7;
    int  num_colors     = -1;     /* -1 = auto-compute */
    int  require_unique = 0;
    unsigned int seed   = (unsigned int)time(NULL);

    /* ── Parse arguments ─────────────────────────────────────────────── */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            rows = cols = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            rows = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-W") == 0 && i + 1 < argc) {
            cols = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            num_colors = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc) {
            seed = (unsigned int)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-u") == 0) {
            require_unique = 1;
        } else if (strcmp(argv[i], "-n") == 0) {
            g_use_color = 0;
        } else if (strcmp(argv[i], "-C") == 0) {
            g_use_color = 2; /* force color even when not a TTY */
        } else if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* Auto-detect color support (unless -C forced it on or -n forced it off) */
    if (g_use_color == 1 && !isatty(STDOUT_FILENO))
        g_use_color = 0;

    /* Auto-compute num_colors if not given: use smaller dimension - 1 */
    if (num_colors < 0) {
        int min_dim = rows < cols ? rows : cols;
        num_colors = min_dim - 1;
        if (num_colors > MAX_COLORS) num_colors = MAX_COLORS;
        if (num_colors < 2)          num_colors = 2;
    }

    /* Basic validation */
    if (rows < 2 || rows > MAX_ROWS) {
        fprintf(stderr, "Error: rows must be 2-%d\n", MAX_ROWS);
        return 1;
    }
    if (cols < 2 || cols > MAX_COLS) {
        fprintf(stderr, "Error: cols must be 2-%d\n", MAX_COLS);
        return 1;
    }
    if (num_colors < 2 || num_colors > MAX_COLORS) {
        fprintf(stderr, "Error: colors must be 2-%d\n", MAX_COLORS);
        return 1;
    }
    if (num_colors * 2 > rows * cols) {
        fprintf(stderr,
                "Error: %d colors require at least %d cells (%dx%d has %d)\n",
                num_colors, num_colors * 2, rows, cols, rows * cols);
        return 1;
    }

    srand(seed);
    printf("Free Flow / Numberlink Generator\n");
    printf("═════════════════════════════════\n");
    printf("Seed: %u  |  Grid: %dx%d  |  Colors: %d\n\n",
           seed, rows, cols, num_colors);

    /* ── Generate puzzle (with optional uniqueness requirement) ────────── */
    Puzzle p;
    int    found    = 0;
    int    attempts = 0;

    do {
        attempts++;
        if (!generate_puzzle(&p, rows, cols, num_colors)) {
            fprintf(stderr,
                    "Error: generator exhausted retries (grid may be too "
                    "constrained for %d colors on a %dx%d board).\n",
                    num_colors, rows, cols);
            return 1;
        }

        if (!validate_puzzle(&p)) continue;

        if (require_unique) {
            int nsols = count_solutions(&p, 2);
            if (nsols >= 2) {
                if (attempts % 500 == 0) {
                    printf("  [attempt %d: ≥2 solutions, retrying…]\n",
                           attempts);
                    fflush(stdout);
                }
                continue;
            }
        }

        if (has_unfilled_solution(&p)) {
            if (attempts % 500 == 0) {
                printf("  [attempt %d: shortcut exists, retrying…]\n", attempts);
                fflush(stdout);
            }
            continue;
        }

        found = 1;
    } while (!found && attempts < 50000);

    if (!found) {
        fprintf(stderr,
                "Error: could not find a uniquely-solvable puzzle "
                "in %d attempts.\n"
                "Tip: try a smaller grid or fewer colors.\n", attempts);
        return 1;
    }

    if (attempts > 1)
        printf("Generated after %d attempt%s.\n\n",
               attempts, attempts == 1 ? "" : "s");

    /* ── Display puzzle ─────────────────────────────────────────────────── */
    print_color_key(num_colors);
    print_puzzle(&p);

    /* ── Display solution ───────────────────────────────────────────────── */
    print_solution(&p, p.solution);

    /* ── Uniqueness verdict ─────────────────────────────────────────────── */
    if (require_unique) {
        printf("✓ Unique solution — well-formed Numberlink puzzle.\n");
    } else {
        int cells = rows * cols;
        int nsols = (cells <= 81) ? count_solutions(&p, 2) : 1;
        if (nsols == 1) {
            printf("✓ Unique solution — well-formed Numberlink puzzle.\n");
        } else {
            printf("⚠ Multiple solutions exist (≥2). "
                   "The puzzle is solvable but not uniquely constrained.\n");
            printf("  Use -u to enforce uniqueness at generation time.\n");
        }
    }

    /* ── Write JSON output ──────────────────────────────────────────────── */
    write_puzzle_json(&p, seed);

    return 0;
}
