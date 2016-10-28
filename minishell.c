#include    <fcntl.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <sys/types.h>
#include    <sys/wait.h>
#include    <string.h>
#include    <unistd.h>
#include    "minishell.h"

int main(int argc, char *argv[], char *envp[])
{       
      startShell(envp);
      return 0;
}

int startShell(char *envp[]) {
      char commandLine[LINE_LEN];
      char *pathv[MAX_PATHS];
      int child_pid;
      int stat;
      pid_t thisChPID;
      prompt_s prompt;
      command_s command;

      initShell(&prompt, &command);    // Shell initialization

      // get all directories from PATH env var
      parsePath(pathv);

      // Main loop
      while(TRUE) {
            // Read the command line
            printPrompt(&prompt);
            readCommand(commandLine);
            // Quit the shell ?
            if( (strcmp(commandLine, "exit") == 0) ||
                (strcmp(commandLine, "quit") == 0) )
                  break;
            // Parse the command line
            command.argc = stringToArray(commandLine, MAX_ARGS, WHITESPACE, command.argv);
            // child process  pid
            // used by parent wait
      
            // Get the full path name
            if((command.name = lookupPath(command.argv, pathv)) == NULL) {
                  printf("Invalid command\n");
                  continue;
            }

            if(DEBUG) {     // Display each parsed token
                  printf("ParseCommand: ");
                  for(int j=0; j<command.argc; j++)
                        printf(" %s, ",command.argv[j]);
                  printf("\n");
            }

            //If file found is not executable
            if(access(command.name, X_OK) != 0)
            {
                  printf("The command \"%s\" is not executable\n", command.name);
                  continue;
            }

            // create child process to execute the command
            // (be careful with the pipes, you need to re-direct the
            // output from the child to stdout to display the results)

            // wait for the child to terminate
            //--- Fork, Exec, Wait process ---
            createRunProc(&command,  envp);
      }
      printf("Terminating Mini Shell\n");
      // Shell termination
      return 0;
}

void initShell(prompt_s * prompt, command_s * command) {
      prompt->shell = shellName;
      prompt->cwd = (lastIndexOf(getenv("PWD"), '/'))+1;
      prompt->user = getenv("USER");
      for(int i=0; i < MAX_ARGS; i++) {
            command->argv[i] = (char *) malloc(MAX_ARG_LEN);
      }
}

char *lookupPath(char ** argv, char ** dirs) {

      char * result = (char *)malloc(MAX_PATH_LEN);   //To store command location
      char * command = (char *)malloc(LINE_LEN);      //To store the command
      int fileExists;                                 //Set if file exists
      
      //Command is the the user's command from the first argument
      command = strcpy(command, argv[0]);
      
      // if absolute path (/) or relative path (. , ..)
      if((command[0] == '/' || command[0] == '.') && (fileExists = access(command, F_OK)))
      {
            result = strcpy(result, argv[0]);   
      }
      else
      {     //Search the path for the file.
            //Attach paths to command until file is found
            //or not found. 
            int dirIndex = 0; 
            do
            {     //Copy and concat onto the result string the 
                  //absolute paths and command.
                  result = strcpy(result, dirs[dirIndex]);
                  strcat(result, "/");
                  strcat(result, command);
                  fileExists = access(result, F_OK); //Exists?
                  dirIndex++;
            }while(dirs[dirIndex] != NULL && fileExists != 0);
      }
      
      //Set the fist argument to the full path of command
      argv[0] = result;

      free(command);
      free(result);

      if(fileExists == 0) {
            return argv[0];
      } else {
            return argv[0] = NULL;
      }
}

void  parsePath(char ** pathv) {
      //Pointer to PATH environment variable
      char * thePath;
        
      //Point to the path
      thePath = getenv("PATH");

      // Initialize all locations in path to null before store
      for(int i=0; i< MAX_PATHS; i++)
            pathv[i] = NULL;
      
      //Tokenize the path and store each token in pathv
      stringToArray(thePath,MAX_PATHS, ":", pathv);
      
      if(DEBUG){
            //Print the contents of pathv after store
            printf("PATH variable:\n");
            for(int i=0; pathv[i] != NULL && i < MAX_PATHS; i++)
                  printf("%s\n", pathv[i]);
      }     
}

void  printPrompt(prompt_s *prompt) {
      printf("%s %s:%s$ ", prompt->shell, prompt->user, prompt->cwd);
}

int   parseCommand(char * commandLine, command_s * command) {
      int argIndex = 0;
      //With string tokenizer, store the first token in first index.
      command->argv[argIndex++] = strtok(commandLine, WHITESPACE);
      
      //Store the rest of the tokens in the other indicies
      while(argIndex < MAX_ARGS && (command->argv[argIndex++] = strtok(NULL, WHITESPACE)) != NULL)
            ;

      return argIndex-1;
}


void  readCommand(char * inputString) {
      if((scanf(" %[^\n]", inputString)) <= 0) {
            perror("Invalid string was read\n");
            inputString = NULL;
      }

}

int stringToArray(char * theString, int max_len, char* delim, char ** theArray)
{
      int arrayIndex = 0;
      //Tokenize first element, store in array     
      theArray[arrayIndex++] = strtok(theString, delim);
        
      //Tokenize the rest of the elements
      while(arrayIndex < max_len && (theArray[arrayIndex++] = strtok(NULL, delim)) != NULL)
            ;
        // for(int i=1; theArray[i-1] != NULL && i < max_len; i++)
        //     theArray[i] = strtok(NULL, delim);
      return arrayIndex-1;
}

void createRunProc(command_s * command, char * envp[])
{
      int child_pid;                // Child process PID
      int stat;                     // used by parent wait
      pid_t thisChPID;              // This process's PID
      short runBackground = FALSE;
      
      thisChPID = fork();
      
      
      if(command->argc > 1 && strcmp(command->argv[command->argc-1],"&")==0 )
      {     
            //printf("& passed as back\n");
            command->argv[command->argc-1] = NULL;
            runBackground = TRUE;
      }     
      if(thisChPID < 0) // Failed fork
      {
            printf("Error, failed to fork\n");
      }
      else if(thisChPID == 0) // Child process
      {
            execve(command->name, command->argv, envp);
            //if(runBackground == TRUE)
                  //printf("DONE [%i]\n", thisChPID);
      }
      else  //Must be parent, wait for child
      {
            if(runBackground == FALSE)
                  child_pid = wait(&stat);
            else
            {
                  printf("BG [%i]\n", thisChPID);
            }
            //if(child_pid == -1)
            //    printf("Process terminated irregularly");
      }      

}

char * lastIndexOf(char * phrase, char key) {
      char * location = NULL;

      do {
            if(*phrase == key){
                  location = phrase;
            }          
      }while(*(++phrase));

      return location;
}
