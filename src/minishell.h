#ifndef MINISHELL_H
#define MINISHELL_H

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#define DEBUG           0
#define FALSE           0
#define TRUE            1
#define LINE_LEN        4096
#define MAX_ARGS        64
#define MAX_PATH_LEN    1024
#define MAX_PATHS       64
#define MAX_CMDS_PIPE   8
#define MAX_PIPELINES   16
#define MAX_TOKENS      512
#define HISTORY_SIZE    100

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Token types */
#define TOK_WORD            0
#define TOK_PIPE            1   /* | */
#define TOK_SEMI            2   /* ; */
#define TOK_AMP             3   /* & */
#define TOK_REDIR_IN        4   /* < */
#define TOK_REDIR_OUT       5   /* > */
#define TOK_REDIR_APPEND    6   /* >> */
#define TOK_REDIR_ERR       7   /* 2> */
#define TOK_REDIR_ERR_APP   8   /* 2>> */

typedef struct {
    char *value;
    int   type;
} Token;

/* A single command with args and redirections */
typedef struct {
    char *argv[MAX_ARGS];   /* NULL-terminated argument vector */
    int   argc;
    char *infile;           /* < filename, NULL if none */
    char *outfile;          /* > or >> filename, NULL if none */
    char *errfile;          /* 2> filename, NULL if none */
    int   append_out;       /* 1 for >> */
    int   append_err;       /* 1 for 2>> */
} Command;

/* A pipeline: cmd1 | cmd2 | ... [&] */
typedef struct {
    Command cmds[MAX_CMDS_PIPE];
    int     num_cmds;
    int     background;
} Pipeline;

/* Global shell state */
typedef struct {
    char   cwd[MAX_PATH_LEN];
    char  *user;
    int    last_status;
    char **envp;
    char   history[HISTORY_SIZE][LINE_LEN];
    int    history_count;
} Shell;

extern Shell  shell;
extern char  *pathv[MAX_PATHS];

/* minishell.c */
int   startShell(char **envp);
void  initShell(char **envp);
void  printPrompt(void);
char *readLine(void);
void  parsePath(void);
void  sigint_handler(int sig);

/* parse.c */
char *expandVariables(const char *input);
int   lex(const char *line, Token *tokens, int max_tokens);
void  freeTokens(Token *tokens, int count);
int   parseTokens(Token *tokens, int count, Pipeline *pipelines, int *num_pipelines);

/* execute.c */
int   runPipelines(Pipeline *pipelines, int count);
int   executePipeline(Pipeline *pipeline);
char *lookupPath(const char *name);

/* builtins.c */
int   isBuiltin(const char *name);
int   executeBuiltin(Command *cmd);

#endif /* MINISHELL_H */
