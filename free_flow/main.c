/*
 * main.c — Free Flow / Numberlink puzzle generator and validator
 *
 * Usage:  free_flow [-s SIZE] [-c COLORS] [-S SEED] [-u] [-n]
 *
 *   -s SIZE    Grid dimension (2–12, default 7)
 *   -c COLORS  Number of color pairs (2–14, default size-1)
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
 * Interior path cells are shown as a middle-dot '·' (or '+' in plain mode).
 */

#include "puzzle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void print_hline(int size)
{
    printf("  +");
    for (int c = 0; c < size; c++) printf("--");
    printf("-+\n");
}

static void print_col_numbers(int size)
{
    printf("   ");
    for (int c = 0; c < size; c++) printf(" %d", c % 10);
    printf("\n");
}

/* ── Public display functions ──────────────────────────────────────────── */

void print_puzzle(const Puzzle *p)
{
    int n = p->size;
    printf("Puzzle (%dx%d, %d color%s):\n",
           n, n, p->num_colors, p->num_colors == 1 ? "" : "s");
    print_hline(n);
    for (int r = 0; r < n; r++) {
        printf("%d |", r % 10);
        for (int c = 0; c < n; c++) {
            int col = p->grid[r][c];
            printf(" ");
            if (col > 0) print_endpoint(col);
            else         print_empty();
        }
        printf(" |\n");
    }
    print_hline(n);
    print_col_numbers(n);
    printf("\n");
}

void print_solution(const Puzzle *p, const int sol[MAX_SIZE][MAX_SIZE])
{
    int n = p->size;
    printf("Solution:\n");
    print_hline(n);
    for (int r = 0; r < n; r++) {
        printf("%d |", r % 10);
        for (int c = 0; c < n; c++) {
            int col = sol[r][c];
            printf(" ");
            if (p->grid[r][c] > 0)  print_endpoint(col);
            else                     print_path_cell(col);
        }
        printf(" |\n");
    }
    print_hline(n);
    print_col_numbers(n);
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

/* ── Usage ─────────────────────────────────────────────────────────────── */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [-s SIZE] [-c COLORS] [-S SEED] [-u] [-n] [-h]\n"
        "\n"
        "  -s SIZE    Grid dimension (2-%d, default 7)\n"
        "  -c COLORS  Number of color pairs (2-%d, default size-1)\n"
        "  -S SEED    Random seed (default: time-based)\n"
        "  -u         Require unique solution (retry until found)\n"
        "  -n         Disable ANSI colors (plain ASCII)\n"
        "  -C         Force ANSI colors even when stdout is not a TTY\n"
        "  -h         Show this help\n"
        "\n"
        "Rules (Numberlink / Flow Free):\n"
        "  Connect each pair of matching colored dots with a path.\n"
        "  Paths may not cross. Every cell must be filled.\n",
        prog, MAX_SIZE, MAX_COLORS);
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    int  size           = 7;
    int  num_colors     = -1;     /* -1 = auto-compute */
    int  require_unique = 0;
    unsigned int seed   = (unsigned int)time(NULL);

    /* ── Parse arguments ─────────────────────────────────────────────── */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
            size = atoi(argv[++i]);
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            num_colors = atoi(argv[++i]);
        else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc)
            seed = (unsigned int)atoi(argv[++i]);
        else if (strcmp(argv[i], "-u") == 0)
            require_unique = 1;
        else if (strcmp(argv[i], "-n") == 0)
            g_use_color = 0;
        else if (strcmp(argv[i], "-C") == 0)
            g_use_color = 2; /* force color even when not a TTY */
        else if (strcmp(argv[i], "-h") == 0) {
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

    /* Auto-compute num_colors if not given */
    if (num_colors < 0) {
        num_colors = size - 1;
        if (num_colors > MAX_COLORS) num_colors = MAX_COLORS;
        if (num_colors < 2)          num_colors = 2;
    }

    /* Basic validation */
    if (size < 2 || size > MAX_SIZE) {
        fprintf(stderr, "Error: size must be 2-%d\n", MAX_SIZE);
        return 1;
    }
    if (num_colors < 2 || num_colors > MAX_COLORS) {
        fprintf(stderr, "Error: colors must be 2-%d\n", MAX_COLORS);
        return 1;
    }
    if (num_colors * 2 > size * size) {
        fprintf(stderr,
                "Error: %d colors require at least %d cells (%dx%d has %d)\n",
                num_colors, num_colors * 2, size, size, size * size);
        return 1;
    }

    srand(seed);
    printf("Free Flow / Numberlink Generator\n");
    printf("═════════════════════════════════\n");
    printf("Seed: %u  |  Grid: %dx%d  |  Colors: %d\n\n",
           seed, size, size, num_colors);

    /* ── Generate puzzle (with optional uniqueness requirement) ────────── */
    Puzzle p;
    int    found    = 0;
    int    attempts = 0;

    do {
        attempts++;
        if (!generate_puzzle(&p, size, num_colors)) {
            fprintf(stderr,
                    "Error: generator exhausted retries (grid may be too "
                    "constrained for %d colors on a %dx%d board).\n",
                    num_colors, size, size);
            return 1;
        }

        /* Structural sanity check */
        if (!validate_puzzle(&p)) {
            /* Should never happen; generation bug guard */
            continue;
        }

        if (require_unique) {
            int nsols = count_solutions(&p, 2);
            if (nsols == 1) {
                found = 1;
            } else if (attempts % 500 == 0) {
                printf("  [attempt %d: puzzle has %s solution(s), retrying…]\n",
                       attempts, nsols >= 2 ? "≥2" : "0");
                fflush(stdout);
            }
        } else {
            found = 1;
        }
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

    /* ── Solve and validate ─────────────────────────────────────────────── */
    printf("Solving…\n");
    int sol[MAX_SIZE][MAX_SIZE];

    if (!solve_puzzle(&p, sol)) {
        fprintf(stderr,
                "ERROR: The generated puzzle has no solution!\n"
                "(This is a generator bug — please report with seed %u)\n",
                seed);
        return 1;
    }

    print_solution(&p, sol);

    /* ── Uniqueness verdict ─────────────────────────────────────────────── */
    /*
     * If we already required uniqueness above, we know the answer.
     * Otherwise, count up to 2 solutions now.
     */
    int nsols = require_unique ? 1 : count_solutions(&p, 2);

    if (nsols == 1) {
        printf("✓ Unique solution — well-formed Numberlink puzzle.\n");
    } else {
        printf("⚠ Multiple solutions exist (≥2). "
               "The puzzle is solvable but not uniquely constrained.\n");
        printf("  Use -u to enforce uniqueness at generation time.\n");
    }

    return 0;
}
