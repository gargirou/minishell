#include "minishell.h"

extern Shell  shell;
extern char  *pathv[];
extern char **environ;

static const char *builtin_names[] = {
    "cd", "pwd", "echo", "export", "unset",
    "env", "exit", "history", "type", "wait", NULL
};

int isBuiltin(const char *name)
{
    if (!name) return 0;
    for (int i = 0; builtin_names[i]; i++)
        if (strcmp(name, builtin_names[i]) == 0) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* cd [-|~|path]                                                        */
/* ------------------------------------------------------------------ */
static int builtin_cd(Command *cmd)
{
    const char *dir;

    if (cmd->argc < 2 || strcmp(cmd->argv[1], "~") == 0) {
        dir = getenv("HOME");
        if (!dir) { fprintf(stderr, "cd: HOME not set\n"); return 1; }
    } else if (strcmp(cmd->argv[1], "-") == 0) {
        dir = getenv("OLDPWD");
        if (!dir) { fprintf(stderr, "cd: OLDPWD not set\n"); return 1; }
        printf("%s\n", dir);
    } else {
        dir = cmd->argv[1];
    }

    /* Save current directory before changing */
    setenv("OLDPWD", shell.cwd, 1);

    if (chdir(dir) != 0) {
        fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
        return 1;
    }

    /* Update shell cwd and PWD env var */
    char *new_cwd = getcwd(NULL, 0);
    if (new_cwd) {
        strncpy(shell.cwd, new_cwd, MAX_PATH_LEN - 1);
        shell.cwd[MAX_PATH_LEN - 1] = '\0';
        setenv("PWD", new_cwd, 1);
        free(new_cwd);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* pwd                                                                  */
/* ------------------------------------------------------------------ */
static int builtin_pwd(Command *cmd)
{
    (void)cmd;
    printf("%s\n", shell.cwd);
    return 0;
}

/* ------------------------------------------------------------------ */
/* echo [-n] [args...]                                                  */
/* ------------------------------------------------------------------ */
static int builtin_echo(Command *cmd)
{
    int newline = 1;
    int start   = 1;

    if (cmd->argc > 1 && strcmp(cmd->argv[1], "-n") == 0) {
        newline = 0;
        start   = 2;
    }

    for (int i = start; i < cmd->argc; i++) {
        if (i > start) putchar(' ');
        /* Process \n \t \\ escape sequences in the string */
        const char *s = cmd->argv[i];
        while (*s) {
            if (*s == '\\' && *(s+1)) {
                s++;
                switch (*s) {
                    case 'n':  putchar('\n'); break;
                    case 't':  putchar('\t'); break;
                    case '\\': putchar('\\'); break;
                    case 'e':  putchar('\033'); break;
                    default:   putchar('\\'); putchar(*s); break;
                }
            } else {
                putchar(*s);
            }
            s++;
        }
    }
    if (newline) putchar('\n');
    return 0;
}

/* ------------------------------------------------------------------ */
/* export [NAME=VALUE ...]                                              */
/* ------------------------------------------------------------------ */
static int builtin_export(Command *cmd)
{
    if (cmd->argc < 2) {
        /* Print all environment variables in export format */
        for (char **e = environ; *e; e++)
            printf("export %s\n", *e);
        return 0;
    }

    for (int i = 1; i < cmd->argc; i++) {
        char *arg = cmd->argv[i];
        char *eq  = strchr(arg, '=');
        if (eq) {
            char name[256];
            int  len = (int)(eq - arg);
            if (len > 255) len = 255;
            strncpy(name, arg, len);
            name[len] = '\0';
            setenv(name, eq + 1, 1);
        }
        /* If no '=', just mark as exported (already in env if set) */
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* unset NAME ...                                                       */
/* ------------------------------------------------------------------ */
static int builtin_unset(Command *cmd)
{
    for (int i = 1; i < cmd->argc; i++)
        unsetenv(cmd->argv[i]);
    return 0;
}

/* ------------------------------------------------------------------ */
/* env                                                                  */
/* ------------------------------------------------------------------ */
static int builtin_env(Command *cmd)
{
    (void)cmd;
    for (char **e = environ; *e; e++)
        printf("%s\n", *e);
    return 0;
}

/* ------------------------------------------------------------------ */
/* exit [N]                                                             */
/* ------------------------------------------------------------------ */
static int builtin_exit(Command *cmd)
{
    int status = shell.last_status;
    if (cmd->argc > 1) status = atoi(cmd->argv[1]);
    printf("Terminating Mini Shell\n");
    exit(status);
}

/* ------------------------------------------------------------------ */
/* history                                                              */
/* ------------------------------------------------------------------ */
static int builtin_history(Command *cmd)
{
    (void)cmd;
    for (int i = 0; i < shell.history_count; i++)
        printf("%4d  %s\n", i + 1, shell.history[i]);
    return 0;
}

/* ------------------------------------------------------------------ */
/* type NAME ...                                                        */
/* ------------------------------------------------------------------ */
static int builtin_type(Command *cmd)
{
    int status = 0;
    for (int i = 1; i < cmd->argc; i++) {
        if (isBuiltin(cmd->argv[i])) {
            printf("%s is a shell builtin\n", cmd->argv[i]);
        } else {
            char *path = lookupPath(cmd->argv[i]);
            if (path) {
                printf("%s is %s\n", cmd->argv[i], path);
                free(path);
            } else {
                fprintf(stderr, "type: %s: not found\n", cmd->argv[i]);
                status = 1;
            }
        }
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* wait [pid]                                                           */
/* ------------------------------------------------------------------ */
static int builtin_wait(Command *cmd)
{
    int status = 0;
    if (cmd->argc > 1) {
        pid_t pid = (pid_t)atoi(cmd->argv[1]);
        waitpid(pid, &status, 0);
    } else {
        /* Wait for all background children */
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
            ;
    }
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Dispatcher                                                           */
/* ------------------------------------------------------------------ */
int executeBuiltin(Command *cmd)
{
    if (!cmd || !cmd->argc) return 1;
    const char *name = cmd->argv[0];

    if (strcmp(name, "cd")      == 0) return builtin_cd(cmd);
    if (strcmp(name, "pwd")     == 0) return builtin_pwd(cmd);
    if (strcmp(name, "echo")    == 0) return builtin_echo(cmd);
    if (strcmp(name, "export")  == 0) return builtin_export(cmd);
    if (strcmp(name, "unset")   == 0) return builtin_unset(cmd);
    if (strcmp(name, "env")     == 0) return builtin_env(cmd);
    if (strcmp(name, "exit")    == 0) return builtin_exit(cmd);
    if (strcmp(name, "history") == 0) return builtin_history(cmd);
    if (strcmp(name, "type")    == 0) return builtin_type(cmd);
    if (strcmp(name, "wait")    == 0) return builtin_wait(cmd);

    fprintf(stderr, "minsh: %s: builtin not implemented\n", name);
    return 1;
}
