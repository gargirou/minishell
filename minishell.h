#define DEBUG 1
#define FALSE 0
#define TRUE 1
#define LINE_LEN 80
#define MAX_ARGS 64
#define MAX_ARG_LEN 64
#define MAX_PATHS 64
#define MAX_PATH_LEN 96
#define WHITESPACE " .,\t&"
#define STD_INPUT 0
#define STD_OUTPUT 1
#define NON_LETHAL_ERROR 0
#ifndef NULL
#define NULL      0
#endif

//Structure for shell command
typedef struct _command_s {
	char *name;
	int argc;
	char *argv[MAX_ARGS];
}command_s;

//Structure for shell prompt
typedef struct _prompt_s {
	char *shell;
	char *cwd;
	char *user;
}prompt_s;

//Main shell functions
int   startShell(char **);
int   prepareCommandForExecution(char *, command_s *, char **);
char  *lookupPath(char *, char **);
void  parsePath(char **);
void  printPrompt(prompt_s *);
void  readCommand(char *);
void  initShell(prompt_s *, command_s *);
int   stringToArray(char *, int, char*, char **);
void  createRunProc(command_s *, char **);
char  *lastIndexOf(char *, char);

//Prompt strings
char *shellName = "minsh";
char *cwd =  "";
char *user = "";