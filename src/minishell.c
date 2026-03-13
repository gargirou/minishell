#include "minishell.h"

Shell  shell;
char  *pathv[MAX_PATHS];

static volatile int got_sigint = 0;

void sigint_handler(int sig)
{
    (void)sig;
    got_sigint = 1;
    write(STDOUT_FILENO, "\n", 1);
}

int main(int argc, char *argv[], char *envp[])
{
    (void)argc;
    (void)argv;
    return startShell(envp);
}

int startShell(char **envp)
{
    /* Signal handling: Ctrl+C prints newline, doesn't kill shell */
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sa.sa_flags   = 0;  /* Don't restart so fgets returns on SIGINT */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGQUIT, SIG_IGN);  /* Ignore Ctrl+\ */

    initShell(envp);
    parsePath();

    while (1) {
        printPrompt();
        fflush(stdout);

        char *line = readLine();
        if (!line) {
            /* Real EOF (Ctrl+D) */
            printf("\n");
            break;
        }

        /* If we got here due to SIGINT, line is empty - loop again */
        if (got_sigint) { got_sigint = 0; free(line); continue; }

        /* Strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';

        /* Skip empty lines and comments */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') { free(line); continue; }

        /* Add to history */
        if (shell.history_count < HISTORY_SIZE) {
            strncpy(shell.history[shell.history_count], line, LINE_LEN - 1);
            shell.history[shell.history_count][LINE_LEN - 1] = '\0';
            shell.history_count++;
        } else {
            memmove(shell.history[0], shell.history[1],
                    (HISTORY_SIZE - 1) * LINE_LEN);
            strncpy(shell.history[HISTORY_SIZE - 1], line, LINE_LEN - 1);
        }

        /* Convenience: "quit" exits the shell */
        if (strcmp(p, "quit") == 0) { free(line); break; }

        /* Expand $VARIABLE and $? */
        char *expanded = expandVariables(line);
        free(line);
        if (!expanded) continue;

        /* Lex */
        Token tokens[MAX_TOKENS];
        int   token_count = lex(expanded, tokens, MAX_TOKENS);
        free(expanded);
        if (token_count == 0) continue;

        /* Parse into pipelines */
        Pipeline pipelines[MAX_PIPELINES];
        int      num_pipelines = 0;
        if (parseTokens(tokens, token_count, pipelines, &num_pipelines) < 0) {
            freeTokens(tokens, token_count);
            shell.last_status = 1;
            continue;
        }

        /* Execute */
        runPipelines(pipelines, num_pipelines);

        /* Free lexed token strings */
        freeTokens(tokens, token_count);
    }

    printf("Terminating Mini Shell\n");
    return shell.last_status;
}

void initShell(char **envp)
{
    shell.envp         = envp;
    shell.last_status  = 0;
    shell.history_count = 0;

    char *cwd = getcwd(NULL, 0);
    if (cwd) {
        strncpy(shell.cwd, cwd, MAX_PATH_LEN - 1);
        shell.cwd[MAX_PATH_LEN - 1] = '\0';
        free(cwd);
    } else {
        const char *pwd = getenv("PWD");
        strncpy(shell.cwd, pwd ? pwd : "/", MAX_PATH_LEN - 1);
    }

    shell.user = getenv("USER");
    if (!shell.user) shell.user = "user";
}

void printPrompt(void)
{
    /* Show only the trailing component of cwd */
    const char *base = strrchr(shell.cwd, '/');
    if (base && *(base + 1) != '\0')
        base++;
    else if (strcmp(shell.cwd, "/") == 0)
        base = "/";
    else
        base = shell.cwd;

    printf("minsh %s:%s$ ", shell.user, base);
}

char *readLine(void)
{
    char *buf = malloc(LINE_LEN);
    if (!buf) return NULL;

    clearerr(stdin);
    if (fgets(buf, LINE_LEN, stdin) == NULL) {
        free(buf);
        if (feof(stdin)) return NULL;  /* Ctrl+D */
        /* EINTR from SIGINT: return empty string so the loop continues */
        buf = malloc(LINE_LEN);
        if (!buf) return NULL;
        buf[0] = '\0';
        return buf;
    }
    return buf;
}

void parsePath(void)
{
    const char *path_env = getenv("PATH");
    if (!path_env) { pathv[0] = NULL; return; }

    char *copy = strdup(path_env);
    if (!copy) { pathv[0] = NULL; return; }

    int   n     = 0;
    char *token = strtok(copy, ":");
    while (token && n < MAX_PATHS - 1) {
        pathv[n++] = strdup(token);
        token = strtok(NULL, ":");
    }
    pathv[n] = NULL;
    free(copy);
}
