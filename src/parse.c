#include "minishell.h"

extern Shell shell;

/*
 * Expand $VAR and $? in input, skipping single-quoted regions.
 * Returns a newly malloc'd string; caller must free.
 */
char *expandVariables(const char *input)
{
    int cap = LINE_LEN * 2;
    char *out = malloc(cap);
    if (!out) return NULL;

    int i = 0, j = 0;

    while (input[i] && j < cap - 1) {
        if (input[i] == '\'') {
            /* Single-quoted: copy verbatim including the quotes */
            out[j++] = input[i++];
            while (input[i] && input[i] != '\'' && j < cap - 1)
                out[j++] = input[i++];
            if (input[i] == '\'') out[j++] = input[i++];
        } else if (input[i] == '\\' && input[i + 1]) {
            /* Backslash escape: copy both characters */
            out[j++] = input[i++];
            out[j++] = input[i++];
        } else if (input[i] == '$') {
            i++;
            if (input[i] == '?') {
                /* $? - last exit status */
                char num[16];
                snprintf(num, sizeof(num), "%d", shell.last_status);
                for (int k = 0; num[k] && j < cap - 1; k++)
                    out[j++] = num[k];
                i++;
            } else if (input[i] == '{') {
                /* ${VAR} */
                i++;
                char name[256];
                int k = 0;
                while (input[i] && input[i] != '}' && k < 255)
                    name[k++] = input[i++];
                name[k] = '\0';
                if (input[i] == '}') i++;
                char *val = getenv(name);
                if (val)
                    for (int m = 0; val[m] && j < cap - 1; m++)
                        out[j++] = val[m];
            } else if (isalpha((unsigned char)input[i]) || input[i] == '_') {
                /* $VARNAME */
                char name[256];
                int k = 0;
                while ((isalnum((unsigned char)input[i]) || input[i] == '_') && k < 255)
                    name[k++] = input[i++];
                name[k] = '\0';
                char *val = getenv(name);
                if (val)
                    for (int m = 0; val[m] && j < cap - 1; m++)
                        out[j++] = val[m];
            } else {
                out[j++] = '$';
            }
        } else {
            out[j++] = input[i++];
        }
    }
    out[j] = '\0';
    return out;
}

/*
 * Lex the line into an array of Token structs.
 * Returns the number of tokens produced.
 * Token values are strdup'd; caller must call freeTokens when done.
 */
int lex(const char *line, Token *tokens, int max_tokens)
{
    int n = 0;
    int i = 0;
    char buf[LINE_LEN];
    int buf_len = 0;
    int in_single = 0, in_double = 0;

#define EMIT_WORD() do {                                    \
    if (buf_len > 0 && n < max_tokens - 1) {              \
        buf[buf_len] = '\0';                               \
        tokens[n].value = strdup(buf);                     \
        tokens[n].type  = TOK_WORD;                        \
        n++; buf_len = 0;                                  \
    }                                                       \
} while (0)

#define EMIT_TOK(s, t) do {                                 \
    if (n < max_tokens - 1) {                              \
        tokens[n].value = strdup(s);                       \
        tokens[n].type  = (t);                             \
        n++;                                                \
    }                                                       \
} while (0)

    while (line[i] && n < max_tokens - 1) {
        char c = line[i];

        if (in_single) {
            if (c == '\'') { in_single = 0; i++; }
            else           { buf[buf_len++] = c; i++; }
            continue;
        }

        if (in_double) {
            if (c == '"') {
                in_double = 0; i++;
            } else if (c == '\\' && (line[i+1] == '"' || line[i+1] == '\\'
                                     || line[i+1] == '$')) {
                buf[buf_len++] = line[i+1]; i += 2;
            } else {
                buf[buf_len++] = c; i++;
            }
            continue;
        }

        /* Line continuation */
        if (c == '\\' && line[i+1] == '\n') { i += 2; continue; }

        /* Backslash escape */
        if (c == '\\' && line[i+1]) {
            buf[buf_len++] = line[i+1]; i += 2; continue;
        }

        if (c == '\'') { in_single = 1; i++; continue; }
        if (c == '"')  { in_double = 1; i++; continue; }

        /* Comment */
        if (c == '#' && buf_len == 0) break;

        /* Whitespace */
        if (c == ' ' || c == '\t') { EMIT_WORD(); i++; continue; }

        /* Pipe */
        if (c == '|') { EMIT_WORD(); EMIT_TOK("|", TOK_PIPE); i++; continue; }

        /* Semicolon */
        if (c == ';') { EMIT_WORD(); EMIT_TOK(";", TOK_SEMI); i++; continue; }

        /* Ampersand */
        if (c == '&') { EMIT_WORD(); EMIT_TOK("&", TOK_AMP); i++; continue; }

        /* Output redirect > or >> or 2> or 2>> */
        if (c == '>') {
            if (buf_len == 1 && buf[0] == '2') {
                /* 2> or 2>> */
                buf_len = 0;
                if (line[i+1] == '>') { EMIT_TOK("2>>", TOK_REDIR_ERR_APP); i += 2; }
                else                  { EMIT_TOK("2>",  TOK_REDIR_ERR);     i++;    }
                continue;
            }
            EMIT_WORD();
            if (line[i+1] == '>') { EMIT_TOK(">>", TOK_REDIR_APPEND); i += 2; }
            else                  { EMIT_TOK(">",  TOK_REDIR_OUT);    i++;    }
            continue;
        }

        /* Input redirect */
        if (c == '<') { EMIT_WORD(); EMIT_TOK("<", TOK_REDIR_IN); i++; continue; }

        buf[buf_len++] = c;
        i++;
    }
    EMIT_WORD();

    tokens[n].value = NULL;
    tokens[n].type  = -1;

#undef EMIT_WORD
#undef EMIT_TOK
    return n;
}

void freeTokens(Token *tokens, int count)
{
    for (int i = 0; i < count; i++) {
        free(tokens[i].value);
        tokens[i].value = NULL;
    }
}

/*
 * Parse tokens[start..end) into a Command.
 * Returns index after last consumed token, or -1 on error.
 */
static int parseCommand(Token *tokens, int start, int end, Command *cmd)
{
    memset(cmd, 0, sizeof(Command));
    int i = start;

    while (i < end) {
        int   type = tokens[i].type;
        char *val  = tokens[i].value;

        if (type == TOK_WORD) {
            if (cmd->argc < MAX_ARGS - 1)
                cmd->argv[cmd->argc++] = val;
            i++;
        } else if (type == TOK_REDIR_IN) {
            i++;
            if (i < end && tokens[i].type == TOK_WORD) {
                cmd->infile = tokens[i].value; i++;
            } else { fprintf(stderr, "minsh: syntax error near '<'\n"); return -1; }
        } else if (type == TOK_REDIR_OUT) {
            i++;
            if (i < end && tokens[i].type == TOK_WORD) {
                cmd->outfile = tokens[i].value; cmd->append_out = 0; i++;
            } else { fprintf(stderr, "minsh: syntax error near '>'\n"); return -1; }
        } else if (type == TOK_REDIR_APPEND) {
            i++;
            if (i < end && tokens[i].type == TOK_WORD) {
                cmd->outfile = tokens[i].value; cmd->append_out = 1; i++;
            } else { fprintf(stderr, "minsh: syntax error near '>>'\n"); return -1; }
        } else if (type == TOK_REDIR_ERR) {
            i++;
            if (i < end && tokens[i].type == TOK_WORD) {
                cmd->errfile = tokens[i].value; cmd->append_err = 0; i++;
            } else { fprintf(stderr, "minsh: syntax error near '2>'\n"); return -1; }
        } else if (type == TOK_REDIR_ERR_APP) {
            i++;
            if (i < end && tokens[i].type == TOK_WORD) {
                cmd->errfile = tokens[i].value; cmd->append_err = 1; i++;
            } else { fprintf(stderr, "minsh: syntax error near '2>>'\n"); return -1; }
        } else {
            break;
        }
    }
    cmd->argv[cmd->argc] = NULL;
    return i;
}

/*
 * Parse tokens[start..end) into a Pipeline.
 * Returns 0 on success, -1 on error.
 */
static int parsePipeline(Token *tokens, int start, int end, Pipeline *pl)
{
    memset(pl, 0, sizeof(Pipeline));
    int cmd_start = start;
    int cmd_idx   = 0;

    for (int i = start; i <= end && cmd_idx < MAX_CMDS_PIPE; i++) {
        if (i == end || tokens[i].type == TOK_PIPE) {
            if (parseCommand(tokens, cmd_start, i, &pl->cmds[cmd_idx]) < 0)
                return -1;
            if (pl->cmds[cmd_idx].argc > 0)
                cmd_idx++;
            cmd_start = i + 1;
        }
    }
    pl->num_cmds = cmd_idx;
    return 0;
}

/*
 * Parse the full token stream into an array of Pipelines.
 * Statements are separated by TOK_SEMI or TOK_AMP.
 * Returns 0 on success, -1 on error.
 */
int parseTokens(Token *tokens, int count, Pipeline *pipelines, int *num_pipelines)
{
    *num_pipelines = 0;
    int stmt_start = 0;

    for (int i = 0; i <= count && *num_pipelines < MAX_PIPELINES; i++) {
        if (i == count
            || tokens[i].type == TOK_SEMI
            || tokens[i].type == TOK_AMP) {

            int background = (i < count && tokens[i].type == TOK_AMP);
            int stmt_end   = i;

            if (stmt_end > stmt_start) {
                Pipeline *pl = &pipelines[*num_pipelines];
                if (parsePipeline(tokens, stmt_start, stmt_end, pl) < 0)
                    return -1;
                if (pl->num_cmds > 0) {
                    pl->background = background;
                    (*num_pipelines)++;
                }
            }
            stmt_start = i + 1;
        }
    }
    return 0;
}
