#include <stdio.h>
#include <stdlib.h>
//include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>

enum custom_commands{command_exit, command_comment, command_cd, command_status};

int foreground_only = 0;

/***********************************************************
*       STRUCTS
***********************************************************/

struct prompt {
    char * command;
    int command_value;
    char * arg[512];
    int arg_count;
    char * input_file;
    char * output_file;
    int run_in_background;
    int lock_foreground;
    int pid;
};

struct background {
    int ids[2000];
    int processCount;
};


/***********************************************************/

void catchSIGINT(int signo) {     
    fflush(stdin); 
    write(STDOUT_FILENO, "terminated by signal 2\n", 24);
    fflush(stdin);    
}

void catchSIGTSTP(int signo) {    
    fflush(stdin);  
    if (foreground_only) {
        write(STDOUT_FILENO, "Exiting foreground-only mode\n", 30);
        foreground_only = 0;
    } else {
        write(STDOUT_FILENO, "Entering foreground-only mode (& is now ignored\n", 49); 
        foreground_only = 1; 
    }
    fflush(stdin); 
}


struct prompt *initialize_prompt(int pid){    
    struct prompt *p = (struct prompt*)malloc(sizeof(struct prompt)); 
    p->command_value = -1;   
    p->run_in_background = 0;
    p->pid = pid;
    p->lock_foreground = 0;
    return p;
}

struct background *initialize_background(){    
    struct background *b = (struct background*)malloc(sizeof(struct background)); 
    b->processCount = 0;
    return b;
}

void resetPrompt(struct prompt *p){
    int i = 0;
    for (i = 0; i < p->arg_count; i++) {
        p->arg[i] = NULL;
    }

    p->arg_count = 0;
    p->command = "";
    p->command_value = -1;  
    p->input_file = "";
    p->output_file = "";
    p->run_in_background = 0;
}

int addBackgroundProcess(struct background *b, int pid) {
    b->ids[b->processCount] = pid;
    b->processCount++;
    return 0;
}

int removeBackgroundProcess(struct background *b, int pid) {
    int idx = -1;
    int i = 0,
        j = 0;

    for (i = 0; i < b->processCount; i++) {
        if (b->ids[i] == pid) {
            idx = i;
        }
    }    

    if (idx != -1) {
        for (j = idx; j < b->processCount - 1; j++) {
            b->ids[j] = b->ids[j+1];
        }
    }

    b->processCount--;

    return 0;
}


void trim(char *str) {
//trim white space off the front and back of a given string
//also trim off new lines

//NOTE:
//  Code here was based off of a few different answers given here: //https://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
//  The code was rewritten and refactored by myself to work for this project.  Comments added by myself to explain code.
    int i;
    int start = 0;
    int end = strlen(str) - 1; 

    //iterate over the string and find where the leading white space ends -> moves left to right in word
    while ( isspace((unsigned char) str[start]) )  {
        start++;
    }

    //iterate over the string and find where the trailing white space ends -> moves right to left in word
    while ( (end >= start) && isspace((unsigned char) str[end]) ) {
        end--;
    }

    //if needed, this shifts the characters back to the start
    //'i' starts at the beginning of the word and moves that to zero in the array [str]
    //this continues until the end of the word, placing the entire word now at the beginning of the array
    for (i = start; i <= end; i++) {
        str[i - start] = str[i];
    }

    //places the null termination at the end of the word, cutting off any trailing white space
    //the variable 'i' still retains the value from the loop above, so the end index is stored there
    str[i - start] = '\0'; 
}


char* addVariablePID(char line[], int PID) {   
    //adapted from here: 
//https://www.quora.com/How-do-I-replace-a-character-in-a-string-with-3-characters-using-C-programming-language
    int length = strlen(line);
    char *variable = "$$";  
    char *new_string = (char *)malloc(sizeof(char) * length);       
    int pid_length = snprintf(NULL, 0,"%d",PID);   
    char sPID[pid_length];
    sprintf(sPID, "%d", PID);
    //printf("PID[s]: %s\n", sPID);
    //printf("PID[d]: %d\n", PID);
    //printf("Len: %d\n", pid_length);

    //index is used to keep track of the new_string insertions
    int i = 0,
        j = 0;       
    int index = 0;    
    
    for(i=0; i<length; i++) {
        //If the character which is to be replaced is found, then in the new string 3 characters will be directly copied, then
        //the counter i will be increased so that the copying of the character which is to be replaced is prevented, and then the
        //for loop is terminated.
        //Else the characters will be simply copied        
    
        if(i > 0 && line[i] == variable[0] && line[i-1] == variable[1] && new_string[index-1] == variable[0]) {
            index--;
            for (j = 0; j < strlen(sPID); j++){            
                new_string[index++] = sPID[j];                
            }            
        }
        else {
            new_string[index++] = line[i];    
        }        
    }
    
    //If there are some characters which are left to be copied, this loop will copy take care of that.
    for(;i<length;i++) {
        new_string[index++] = line[i];
    }
    //printf("Old line: %s\n", line);
    //printf("New line: %s\n", new_string);
    return new_string;
}


int getCommandInt(char *command) {
    char first_char[2];
    
    if (strcmp (command, "cd") == 0) {
        return command_cd;
    }

    if (strcmp (command, "exit") == 0) {
        return command_exit;
    }

    //////////////////////////
    //'NO ACTION' COMMANDS
    //////////////////////////
    if (strcmp (command, "") == 0) {
        return command_comment;
    }    
    
    strncpy(first_char, command, 1);
    first_char[1] = '\0';
    if (strcmp (first_char, "#") == 0) {
        return command_comment;
    }
    //////////////////////////

    if (strcmp (command, "status") == 0) {
        return command_status;
    }

    return -1;
}

int read_shell_input(struct prompt * p, char input[]){
    char * token;
    char prev_token[256];

    token = strtok(input, " ");
    if (token == 0) {
        p->command_value = getCommandInt("");        
    } else {    
        p->command_value = getCommandInt(token); 
    }
    
    if (p->command_value != command_comment) {
        p->command = token;
        p->arg[p->arg_count] = token;
        p->arg_count++;
    }    
    
    token = strtok (NULL, " ");

    while (token != NULL){
        if (strcmp (token, "<") == 0) {
            token = strtok (NULL, " ");
            p->input_file = token;
        }
        else if (strcmp (token, ">") == 0) {
            token = strtok (NULL, " ");
            p->output_file = token;
        }
        else {
            p->arg[p->arg_count] = token;
            p->arg_count++;
        }
        strcpy(prev_token, token); 
        //printf("token: %s\n", token);
        token = strtok (NULL, " ");
    }

    if (strcmp(prev_token, "&") == 0) {
        p->arg_count--;
        p->arg[p->arg_count] = NULL;
        if (foreground_only == 1) {
            p->run_in_background = 0;            
        } else {
            p->run_in_background = 1;
        }
    } else {
        p->arg[p->arg_count] = NULL;
    }   

    return 1;
}

void _printPromptDetails(struct prompt *p) {
    int i = 0;
    printf("Command: %s\n",p->command);
    for(i = 0; i < p->arg_count; i++){
        printf("Argument %d: %s\n", i, p->arg[i]);
    }
    printf("Input: %s\n", p->input_file);
    printf("Output: %s\n", p->output_file);
    printf("Background[0/1]: %d\n", p->run_in_background);
}

int clearPromptArguments(struct prompt *p) {
    //resets all array arguments to NULL
    //otherwise the execlp() function will read old arguments
    //execlp reads all none NULL slots, even from past commands
    int i = 0;
    for (i = 0; i < p->arg_count; i++){
        p->arg[i] = NULL;
    }
    p->arg_count = 0;
    return 0;
}

int runShell() {        
    
    pid_t spawnPid = -5;
    int childExitMethod = -5,         
        pid = getpid(),
        exitValue = -5, 
        cem = -5,    
        priorID = 0,    
        sourceFD,
        targetFD,
        i = 0,
        j = 0,
        result;
     
    int  cstatus;
    char user_input[2048];      
    struct prompt *p = initialize_prompt(pid);    
    struct background *b = initialize_background();

    struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};
    
    SIGINT_action.sa_handler = catchSIGINT;
    SIGINT_action.sa_flags = 0;     
    sigfillset(&SIGINT_action.sa_mask);         
    sigaction(SIGINT, &SIGINT_action, NULL);    

    SIGTSTP_action.sa_handler = catchSIGTSTP;
    SIGTSTP_action.sa_flags = 0;
    sigfillset(&SIGTSTP_action.sa_mask);    
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    
    while( p->command_value != command_exit ) {
        resetPrompt(p);
        
        //check all background processes
        //display message if done and remove from tracking array
        for (j = 0; j < b->processCount; j++) {
           if ( waitpid(b->ids[j], &childExitMethod, WNOHANG) != 0) {
                printf("background pid %d is done: exit value %d\n", b->ids[j], childExitMethod);
                removeBackgroundProcess(b, b->ids[j]);
            }
        }

        printf(":");
        fflush(stdin);
        
        //get user command and match to command number
        fgets(user_input, 2048, stdin);
        trim(user_input);  
        strcpy(user_input, addVariablePID(user_input, pid)) ;           
        read_shell_input(p, user_input);

        //_printPromptDetails(p);
        waitpid(priorID, &cem, WNOHANG); 

        switch(p->command_value) {

            case command_exit  :
                //
                //BUILD -> KILL ALL PROCESSES IN background[]
                //Then allow while loop to naturally exit
                //
                for (i = 0; i < b->processCount; i++){}
                    
                break; 

            case command_comment :
                //do nothing --> used for comments and blank input                
                break;            
                
            case command_status  :                
                //get status function   
                waitpid(priorID, &cem, WNOHANG);            
                if (exitValue == -5 && cem == -5) {
                    printf("exit value %d\n", 0);
                }                
                else 
                if (WIFEXITED(cem)) {                   
                    exitValue = WEXITSTATUS(cem);
                    printf("exit value %d\n", exitValue);
                }
                else {
                    int termSignal = WTERMSIG(cem);
                    printf("terminated by signal %d\n", termSignal);
                }
                break; 
            
            case command_cd :
                if( p->arg_count == 1 ) {
                    chdir(getenv("HOME"));
                } else {
                    chdir(p->arg[1]);
                }
                break;
            
            default :
                exitValue = 0;
                spawnPid = fork();  
                            
                if (spawnPid == -1) { //            
                    perror("Hull Breach!\n");
                    exit(1);

                } else if (spawnPid == 0) { // the child process             
                   
                    if (p->run_in_background) {                       
                        //if user selects to run in background and no output is indicated
                        //then redirect the output to /dev/null
                        if (strcmp (p->output_file, "") == 0 ) {
                            p->output_file = "/dev/null";
                        }                                                        
                    }

                    //Input file --> if specified, then set to this
                    if (strcmp (p->input_file, "") != 0) {
                        sourceFD = open(p->input_file, O_RDONLY);
                        //printf("sourceFD == %d\n", sourceFD); // Written to terminal

                        result = dup2(sourceFD, 0);
                        if (result == -1) { 
                            perror("source dup2()"); exit(2); 
                        }
                    }
                    
                    //Output file --> if specified, then set to this
                    if (strcmp (p->output_file, "") != 0) {
                        targetFD = open(p->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (targetFD == -1) { perror("target open()"); exit(1); }
                        //printf("targetFD == %d\n", targetFD); // Written to terminal

                        result = dup2(targetFD, 1);
                        if (result == -1) { perror("target dup2()"); exit(2); }
                    }
                    ////////////////////////////////////////////////////////   
                    execvp(p->arg[0],p->arg); 

                    printf("%s: no such file or directory\n", p->arg[0]);               
                    exit(1);
                    
                } else {                    
                    
                    if (p->run_in_background != 1) {  
                        priorID = spawnPid;                                     
                        waitpid(spawnPid, &childExitMethod, 0);                  
                        cem = childExitMethod;                                      
                    }               
                }           
                break;  
            }
        //create prompt and flush 
        if (p->run_in_background){
            printf("background pid is %d\n", spawnPid); 
            addBackgroundProcess(b, spawnPid);
        }
        clearPromptArguments (p);
        fflush(stdin);
    }
    free(p);
    return 0;
}

int main(void) {
  runShell();

//DELETE MSG IN PROD
  printf("---PROGRAM ENDED--\n");  
  return 0;
}