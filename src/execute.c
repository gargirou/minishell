#include "minishell.h"

extern Shell  shell;
extern char  *pathv[];

/*
 * Look up a command name in pathv.
 * Returns a malloc'd full path string, or NULL if not found.
 * Caller must free the returned string.
 */
char *lookupPath(const char *name)
{
    if (!name || !name[0]) return NULL;

    /* Absolute or relative path */
    if (name[0] == '/' || name[0] == '.') {
        if (access(name, F_OK) == 0)
            return strdup(name);
        return NULL;
    }

    /* Search each directory in pathv */
    for (int i = 0; pathv[i] != NULL; i++) {
        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s/%s", pathv[i], name);
        if (access(full, X_OK) == 0)
            return strdup(full);
    }
    return NULL;
}

/*
 * Apply file redirections for a builtin running in the shell process.
 * Saves original fds into saved_* (set to -1 if not redirected).
 * Returns 0 on success, -1 on error.
 */
static int applyRedirects(Command *cmd, int *saved_in, int *saved_out, int *saved_err)
{
    *saved_in = *saved_out = *saved_err = -1;

    if (cmd->infile) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) { perror(cmd->infile); return -1; }
        *saved_in = dup(STDIN_FILENO);
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (cmd->outfile) {
        int flags = O_WRONLY | O_CREAT | (cmd->append_out ? O_APPEND : O_TRUNC);
        int fd = open(cmd->outfile, flags, 0644);
        if (fd < 0) { perror(cmd->outfile); return -1; }
        *saved_out = dup(STDOUT_FILENO);
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    if (cmd->errfile) {
        int flags = O_WRONLY | O_CREAT | (cmd->append_err ? O_APPEND : O_TRUNC);
        int fd = open(cmd->errfile, flags, 0644);
        if (fd < 0) { perror(cmd->errfile); return -1; }
        *saved_err = dup(STDERR_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
    return 0;
}

static void restoreRedirects(int saved_in, int saved_out, int saved_err)
{
    if (saved_in  >= 0) { dup2(saved_in,  STDIN_FILENO);  close(saved_in);  }
    if (saved_out >= 0) { dup2(saved_out, STDOUT_FILENO); close(saved_out); }
    if (saved_err >= 0) { dup2(saved_err, STDERR_FILENO); close(saved_err); }
}

/*
 * Execute a pipeline.
 * Returns the exit status of the last command.
 */
int executePipeline(Pipeline *pipeline)
{
    int num = pipeline->num_cmds;
    if (num == 0) return 0;

    /* ---- Single command (no pipes) ---- */
    if (num == 1) {
        Command *cmd = &pipeline->cmds[0];
        if (!cmd->argc) return 0;

        if (isBuiltin(cmd->argv[0])) {
            int saved_in, saved_out, saved_err;
            if (applyRedirects(cmd, &saved_in, &saved_out, &saved_err) < 0)
                return 1;
            int status = executeBuiltin(cmd);
            restoreRedirects(saved_in, saved_out, saved_err);
            return status;
        }

        char *path = lookupPath(cmd->argv[0]);
        if (!path) {
            fprintf(stderr, "minsh: %s: command not found\n", cmd->argv[0]);
            return 127;
        }

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); free(path); return 1; }

        if (pid == 0) {
            signal(SIGINT,  SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            if (cmd->infile) {
                int fd = open(cmd->infile, O_RDONLY);
                if (fd < 0) { perror(cmd->infile); exit(1); }
                dup2(fd, STDIN_FILENO); close(fd);
            }
            if (cmd->outfile) {
                int flags = O_WRONLY | O_CREAT | (cmd->append_out ? O_APPEND : O_TRUNC);
                int fd = open(cmd->outfile, flags, 0644);
                if (fd < 0) { perror(cmd->outfile); exit(1); }
                dup2(fd, STDOUT_FILENO); close(fd);
            }
            if (cmd->errfile) {
                int flags = O_WRONLY | O_CREAT | (cmd->append_err ? O_APPEND : O_TRUNC);
                int fd = open(cmd->errfile, flags, 0644);
                if (fd < 0) { perror(cmd->errfile); exit(1); }
                dup2(fd, STDERR_FILENO); close(fd);
            }

            execve(path, cmd->argv, shell.envp);
            perror(cmd->argv[0]);
            exit(127);
        }

        free(path);
        if (pipeline->background) { printf("[bg] %d\n", pid); return 0; }

        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))   return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return 0;
    }

    /* ---- Multi-command pipeline ---- */
    int   pipes[MAX_CMDS_PIPE - 1][2];
    pid_t pids[MAX_CMDS_PIPE];

    for (int i = 0; i < num - 1; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); return 1; }
    }

    for (int i = 0; i < num; i++) {
        Command *cmd = &pipeline->cmds[i];

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }

        if (pid == 0) {
            signal(SIGINT,  SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            /* Wire up pipe ends */
            if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            if (i < num - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            /* Close all pipe fds to avoid blocking */
            for (int j = 0; j < num - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            /* Apply file redirections (override pipe if present) */
            if (cmd->infile) {
                int fd = open(cmd->infile, O_RDONLY);
                if (fd < 0) { perror(cmd->infile); exit(1); }
                dup2(fd, STDIN_FILENO); close(fd);
            }
            if (cmd->outfile) {
                int flags = O_WRONLY | O_CREAT | (cmd->append_out ? O_APPEND : O_TRUNC);
                int fd = open(cmd->outfile, flags, 0644);
                if (fd < 0) { perror(cmd->outfile); exit(1); }
                dup2(fd, STDOUT_FILENO); close(fd);
            }
            if (cmd->errfile) {
                int flags = O_WRONLY | O_CREAT | (cmd->append_err ? O_APPEND : O_TRUNC);
                int fd = open(cmd->errfile, flags, 0644);
                if (fd < 0) { perror(cmd->errfile); exit(1); }
                dup2(fd, STDERR_FILENO); close(fd);
            }

            /* Execute - builtins run in subprocess within a pipeline */
            if (isBuiltin(cmd->argv[0])) {
                exit(executeBuiltin(cmd));
            }

            char *path = lookupPath(cmd->argv[0]);
            if (!path) {
                fprintf(stderr, "minsh: %s: command not found\n", cmd->argv[0]);
                exit(127);
            }
            execve(path, cmd->argv, shell.envp);
            perror(cmd->argv[0]);
            exit(127);
        }

        pids[i] = pid;
    }

    /* Parent: close all pipe ends */
    for (int i = 0; i < num - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (pipeline->background) {
        printf("[bg]");
        for (int i = 0; i < num; i++) printf(" %d", pids[i]);
        printf("\n");
        return 0;
    }

    /* Wait for all children; return last status */
    int last_status = 0;
    for (int i = 0; i < num; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == num - 1) {
            if (WIFEXITED(status))   last_status = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
        }
    }
    return last_status;
}

/* Execute all pipelines from a parsed line */
int runPipelines(Pipeline *pipelines, int count)
{
    int status = 0;
    for (int i = 0; i < count; i++) {
        status = executePipeline(&pipelines[i]);
        shell.last_status = status;
    }
    return status;
}
