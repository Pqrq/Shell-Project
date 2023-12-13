/** Including necessary .h files **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <signal.h>

/** Function prototypes **/
int parser(char* input);
int spawn (char* program, char** arg_list);
int isInPath(const char *token);
void removeQuotes(char *str);

/** Global variables **/
char lastlyExecutedCommand[255] = ""; // Our "lastly executed command" (will be explained in more detail later)
int numOfTotal = 0; // Number of total process spawned (terminated or not) (excluding main process)
int numOfReaped = 0; // Number of total processes reaped
int *reaPointer = &numOfReaped; // Pointer to the above variable, details will be given
int error; // An integer error flag. 1 indicates error, 0 indicates non-error

/** Our helping functions **/
// As its name shows, it removes the first and last quotes of a string input str
void removeQuotes(char *str) {
    char *src = str, *dst = str;
    while (*src) {
        if ((*src != '"') || ((*src == '"')&&(src!=str)&&(*(src+1) != '\0'))) {
            *dst = *src;
            dst++;
        }
        src++;
    }
    *dst = '\0';  // Null-terminate the modified string
}
// Creates a child process, executes a program with the given argument list,
// which are given in the parameters of the function
int spawn (char* program, char** arg_list) {
    // Forking the parent, creating a child
    pid_t child_pid;
    child_pid = fork ();
    if (child_pid != 0) {
        // Parent enters here
        int status; // Success-failure status of our child process
        /* This is the parent process. It waits for the child and returns child's id */
        waitpid(child_pid, &status, 0);

        // Check if the child process exited abnormally
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Child process could not be executed successfully\n"); // Setting error message
            error = 1; // Setting error flag
            (*reaPointer) -= 1; // Temporary decrease for the below increase case
        }
        (*reaPointer) += 1; // Increasing the number of reaped processes by 1 in no error case, else do not increment
        return child_pid;
    }
    else {
        // This is the child process, which executes the program
        if (execvp(program, arg_list) == -1) {
            /* The execvp function returns only if an error occurs.  */
            // Writing an error message and abort
            fprintf (stderr, "An error occurred in execvp\n");
            abort ();
        }
    }
}
// Checks whether a command in inside the path or not (excluding bello command)
int isInPath(const char *token) {
    char *path = getenv("PATH"); // Getting the path environment variable

    // If path is null, return error, set error flag and corresponding error message
    if (path == NULL) {fprintf(stderr, "PATH environment variable not found\n");error=1;return 0;}

    // Copying the path and taking the part before first :
    char *pathCopy = strdup(path);
    char *pathToken = strtok(pathCopy, ":");

    // Searching the command in the path, if found return 1, otherwise it gets out of the while loop
    // During and after the procedure, we also need to deallocate the allocated memory spaces as well
    while (pathToken != NULL) {
        char *executablePath = malloc(strlen(pathToken) + strlen(token) + 2);
        strcpy(executablePath, pathToken);
        strcat(executablePath, "/");
        strcat(executablePath, token);

        // Command found in the path
        if (access(executablePath, X_OK) == 0) {
            //printf("%s found in PATH\n", token);
            free(executablePath);
            free(pathCopy);
            return 1;
        }
        free(executablePath);
        pathToken = strtok(NULL, ":");
    }
    free(pathCopy);
    // In case of bello, no need to give error, as it is an exceptional case
    // If it is not bello and not found in the path, follow the similar error procedures as before
    // (Indeed, bello is not in our path, but we don't want this function to print error
    // each time we run a bello command. That's the reason why we are doing this exception)
    if (strcmp(token,"bello") != 0) {
        fprintf(stderr, "Error: '%s' not found in the PATH\n", token);
        error = 1;
        return 0;
    }
}
/** Our main function **/
int main() {

    while (1) {
        // Initializing strings which are holding our input and hostname
        char userInput[1024];
        char hostName[1024];

        // This just creates the alias_config_file
        FILE *first = fopen("alias_config_file.txt", "a");
        fclose(first);

        // Getting the hostname and corresponding error procedures in case of an error
        if (gethostname(hostName, sizeof(hostName)) != 0) {
            perror("Error getting hostname");
            error = 1;
            return 1;
        }

        // Printing the prompt
        printf("%s@%s %s --- ", getenv("USER"), hostName, getenv("PWD"));

        // Taking the input from the user, if exit signal, then terminate the shell
        if (fgets(userInput, sizeof(userInput), stdin) == NULL) {
            break;
        }

        // Remove the newline character, if exists
        userInput[strcspn(userInput, "\n")] = '\0';

        // Our key function which takes the input, parses it and does
        // the corresponding tasks. Details will be explained
        parser(userInput);

        // Only the last "successfully" executed command will be
        // written to the "lastly executed command" string. This part
        // of the code handles the input error cases
        if (error == 0) {
            strcpy(lastlyExecutedCommand,userInput);
        }
        if (error == 1) {
            printf("!Error occured!\n");
            (*reaPointer)++; // Increasing the number of reaped processes by 1
        }

    }
    return 0;
}

/** Our key function. Takes the input, parses it into tokens **/
/** and does the corresponding executions inside. In case of error **/
/** it also handles according error procedures as well. **/
int parser(char* input) {
    error = 0; // Initialized error flag
    char* tokens[255]; // List of tokens
    int index = 0; // Will declare the ending index (which has \0) of our input string, initialized as 0
    char partofString[1024] = ""; // The temporary token holder string. Details will be explained
    int indexOfFirstQuotation = -1; // Index of the first quotation mark, if not exists, initialized as -1
    int indexOfLastQuotation = -1; // Index of the last quotation mark, if not exists, initialized as -1
    int areWeInString; // Checks if we are inside a string while reading the input.
    // 0 = we are not inside a string, 1 = we are inside a string
    int stillWhiteSpace = 0; // Checks if we are still reading a whitespace inside the input

    // Checking the (background) zombie processes, reaps them and increase the number of
    // reaped processes by 1 each time we reap a zombie process
    pid_t terminated_child;
    while ((terminated_child = waitpid(-1, NULL, WNOHANG)) > 0) {
        (*reaPointer) += 1;
    }

    // Fixing the PATH environment variable, in order to prevent possible errors
    char* path = getenv("PATH");
    setenv("PATH", path, 1);

    for (int i=0;i <= strlen(input); i++) {if (input[i] == '"') {indexOfFirstQuotation = i;break;}}
    for (int i= strlen(input);i >= 0; i--) {if (input[i] == '"') {indexOfLastQuotation = i;break;}}

    // If quotes do not match, give error
    if (indexOfFirstQuotation==indexOfLastQuotation && indexOfFirstQuotation != -1) {
        for (int j = 0; j < index; j++) {
            free(tokens[j]); // Freeing the tokens
        }
        error = 1; // Setting the error flag
        printf("%s\n", "Outside string error!"); // Corresponding error message
        return 1;
    }

    // Reading the input and parsing it accordingly
    for (int i = 0; i <= strlen(input); i++) {
        char ch = input[i];
        if (indexOfFirstQuotation <= i && i <= indexOfLastQuotation) {areWeInString=1;} else {areWeInString=0;}

        // We are at the end of our input
        if (ch == '\0') {
            // Still inside a string -> ERROR!
            if (areWeInString==1) {
                for (int j = 0; j < index; j++) {
                    free(tokens[j]); // Freeing the tokens
                }
                error = 1; // Setting the error flag
                printf("%s\n", "Inside string error!"); // Corresponding error message
            }
            // Graceful ending
            else {
                // If the end is not consecutive whitespace
                if (!stillWhiteSpace) {
                    tokens[index] = strdup(partofString);
                    index++;
                    strcpy(partofString, "");
                    tokens[index] = NULL;
                }
                // If the end is "still whitespace"
                else {
                    tokens[index] = NULL;
                }

                /** F.d.p (for debugging purposes) **/
                //for (int j = 0; j < index; j++) {
                //    printf("Token %d is %s\n", j, tokens[j]);
                //}
                continue;
            }
        }

        // We are at a whitespace which is not inside a string
        if ((ch == ' ') && (areWeInString == 0)) {
            // Still whitespace
            if (stillWhiteSpace) {
                continue;
            }
            // Not consecutive whitespace
            stillWhiteSpace = 1;
            tokens[index] = strdup(partofString);
            index++;
            strcpy(partofString, "");
            continue;
        }

        // Double quotation mark
        if (ch == '"') {
            if (input[i-1] == '=') {
                stillWhiteSpace = 0;
                tokens[index] = strdup(partofString);
                index++;
                strcpy(partofString, "");
                strncat(partofString, &ch, 1);
                continue;
            }
            else {
                stillWhiteSpace = 0;
                strncat(partofString, &ch, 1);
                continue;
            }
        }
        // Equality symbol, but the previous is not a space
        if ((ch == '=')&&(input[i-1] != ' ')) {
            stillWhiteSpace = 0;
            tokens[index] = strdup(partofString);
            index++;
            strcpy(partofString, "=");
        }
        // The remaining general cases, like usual characters, numbers, letters, etc.
        else {
            if (input[i-1] == '=') {
                stillWhiteSpace = 0;
                tokens[index] = strdup(partofString);
                index++;
                strcpy(partofString, "");
                strncat(partofString, &ch, 1);
                continue;
            }
            else {
                stillWhiteSpace = 0;
                strncat(partofString, &ch, 1);
                continue;
            }
        }
    }

    // If an error occurs in the parsing part, do not do any execution
    // just exit the function by returning 1 immediately
    if (error == 1) {
        return 1;
    }

    /** The parsing part has finished, the rest belongs to the execution **/
    // Indexes of >,>>,>>>,& (if not exist in the input, initialized as -1)
    int indexOfBackground = -1;
    int indexOfRedirection = -1;
    int indexOfAppend = -1;
    int indexOfReverseAppend = -1;

    // Checking these indexes
    for (int a = 0; a<index; a++) {
        if (strcmp(tokens[a], "&") == 0) {indexOfBackground = a; continue;}
        if (strcmp(tokens[a], ">") == 0) {indexOfRedirection = a; continue;}
        if (strcmp(tokens[a], ">>") == 0) {indexOfAppend = a; continue;}
        if (strcmp(tokens[a], ">>>") == 0) {indexOfReverseAppend = a; continue;}
    }
    /** If we do not write anything, do nothing , just open a new prompt **/
    if (strcmp(tokens[0], "") == 0) {
        return 0;
    }
    /** If the first command is exit, the rest is unimportant, just get out of the program **/
    if (strcmp(tokens[0], "exit") == 0) {
        exit(1);
    }
    // A flag which checks if we have entered any of these below cases, will be used later on
    int hasEnteredYet = 0;
    /** Bello case (one token which is bello) **/
    if ((strcmp(tokens[0], "bello") == 0) && (index == 1)) {
        hasEnteredYet = 1; // Setting the flag

        // Opening bello.txt, the contents of bello will be stored in this file
        FILE* bello = fopen("bello.txt", "w");

        // Checking possible errors
        if (bello == NULL) {
            perror("Error opening output.txt");
            return 1;
        }

        /** The required information inside bello are below **/

        /** Username **/
        fprintf(bello, "%s\n", getenv("USER"));

        /** Hostname **/
        char hostName[255];
        if (gethostname(hostName, sizeof(hostName)) != 0) {
            perror("Error getting hostname");
            error = 1;
            return 1;
        }
        fprintf(bello, "%s\n", hostName);

        /** Last executed command **/
        fprintf(bello, "%s\n", lastlyExecutedCommand);

        /** TTY **/
        char *terminal_name = ttyname(0);
        if (terminal_name != NULL) {
            fprintf(bello, "%s\n", terminal_name);
        }
        else {
            perror("Error getting terminal name");
            error = 1;
            return EXIT_FAILURE;
        }

        /** Current shell name **/
        fprintf(bello, "%s\n", getenv("SHELL"));

        /** Home location **/
        fprintf(bello, "%s\n", getenv("HOME"));

        /** Current time and date **/
        time_t current_time;
        current_time = time(NULL);
        char* time_string = ctime(&current_time);
        fprintf(bello, "%s", time_string);

        /** Current number of processes being executed (will be modified) **/
        fprintf(bello, "%d\n", numOfTotal-(*reaPointer)+1);

        /** The required information inside bello are above **/

        // Close bello.txt
        fclose(bello);

        // Opening bello.txt once again, the contents of bello.txt are in this file
        int bello_descriptor = open("bello.txt", O_RDONLY);

        // Check for errors in opening the file
        if (bello_descriptor == -1) {
            perror("Error opening bello.txt");
            exit(EXIT_FAILURE);
        }

        // Read and write the contents of bello.txt to the standard output
        char readLine[1024];
        ssize_t bytesRead;
        while ((bytesRead = read(bello_descriptor, readLine, sizeof(readLine))) > 0) {
            // Writing the contents onto stdout, 1 is the file descriptor of the standard output
            write(1, readLine, bytesRead);
        }

        // Close the file descriptor for reading
        close(bello_descriptor);

        return 0;
    }

    /** "Assigning alias" case **/
    if (strcmp(tokens[0], "alias") == 0) {
        hasEnteredYet = 1; // Setting the flag
        // Checks if that variable has already been defined as an alias of some command
        int isAlreadyDefined = 0;

        // Extracting key and value, removing their possible quotes
        char* variable = tokens[1];
        char* value = tokens[3];

        removeQuotes(variable);
        removeQuotes(value);

        // Alias configuration file opened in read mode
        // All aliases and their corresponding commands are stored in this file
        FILE *alias_config_file = fopen("alias_config_file.txt", "r");
        // Error check
        if (alias_config_file == NULL) {
            perror("Error opening the file");
            error = 1;
            return 1;
        }

        // Alias configuration file opened in write mode
        FILE *temp_file = fopen("temp_file.txt", "w");
        // Error check
        if (temp_file == NULL) {
            perror("Error opening the file");
            error = 1;
            fclose(alias_config_file);
            return 1;
        }

        // Reading the aliases' variable name (serves as a "key")
        // and its corresponding command (serves as a "variable")
        char alias_key_value[256];

        // Iterating through the file to find the aliased command
        while (fgets(alias_key_value, sizeof(alias_key_value), alias_config_file) != NULL) {
            // Create a copy of the line to tokenize
            char lineCopy[256];
            strcpy(lineCopy, alias_key_value);
            // Variable name being read
            char *readVariable = strtok(lineCopy, "=");
            removeQuotes(readVariable);
            // Checking whether the variable has already been written before
            if (strcmp(readVariable, tokens[1]) == 0) {
                isAlreadyDefined = 1;
                fprintf(temp_file, "%s=%s\n", tokens[1], tokens[3]);
            } else {
                fprintf(temp_file, "%s", alias_key_value);
            }
        }
        // If it is a new variable, just add it at the end of the alias_config_file.txt
        if (!isAlreadyDefined) {
            fprintf(temp_file, "%s=%s\n", tokens[1], tokens[3]);
        }

        // Close the files
        fclose(temp_file);
        fclose(alias_config_file);

        // Replace the original file with the temporary file
        if (rename("temp_file.txt", "alias_config_file.txt") != 0) {
            perror("Error renaming file");
            error = 1;
            return 1;
        }
        return 0;
    }


    /** Executing the aliased command **/
    // Alias configuration file opened in read mode
    FILE *alias_config_file = fopen("alias_config_file.txt", "rw");
    // Error check
    if (alias_config_file == NULL) {
        perror("Error opening the file");
        error = 1;
        return 1;
    }

    char lineBeingRead[256]; // As its name says, buffer for lines being read through the below while loop
    // Iterating through the file to find the aliased command
    while (fgets(lineBeingRead, sizeof(lineBeingRead), alias_config_file) != NULL) {
        // Create a copy of the line to tokenize
        char lineCopy[256];
        strcpy(lineCopy, lineBeingRead);

        // Variable name being read
        char *readVariable = strtok(lineCopy, "=");
        removeQuotes(readVariable);

        // Meaning our command is aliased, just execute the command
        if (strcmp(readVariable, tokens[0]) == 0) {
            hasEnteredYet = 1;

            // The command which will be executed
            char *storedCommand = strtok(NULL, "=");
            if (storedCommand != NULL) {
                // Remove the newline character from the storedCommand
                size_t len = strlen(storedCommand);
                if (len > 0 && storedCommand[len - 1] == '\n') {
                    storedCommand[len - 1] = '\0';
                }
                // Closing the alias_config_file
                fclose(alias_config_file);

                // Adding the rest arguments for the alias
                int totalLength = 0;
                for (int a = 0; a < index; a++) {
                    if (a == 0) {
                        totalLength += strlen(storedCommand);
                    } else {
                        totalLength += strlen(tokens[a]) + 1;
                    }
                }

                // Dynamically allocate memory for wrappedCommand, which is the replaced version
                // of the stored command, where the alias key is changed with alias value
                char *wrappedCommand = (char *)malloc(totalLength + 1); // +1 for null terminator

                // Handle memory allocation failure
                if (wrappedCommand == NULL) {
                    fprintf(stderr, "Memory allocation failed\n");
                    error = 1;
                    exit(EXIT_FAILURE);
                }

                // Initialize the memory to avoid undefined behavior
                wrappedCommand[0] = '\0';

                // Wrapping up the command stored inside alias
                for (int b = 0; b < index; b++) {
                    if (b == 0) {
                        strcat(wrappedCommand, storedCommand);
                        strcat(wrappedCommand, " ");
                    } else {
                        strcat(wrappedCommand, tokens[b]);
                        if (b < index - 1) {
                            strcat(wrappedCommand, " ");
                        } else {
                            // Do nothing
                        }
                    }
                }

                // Removing the possible quotes from the wrapped command
                removeQuotes(wrappedCommand);
                // Sending the wrapped command to parser function
                parser(wrappedCommand);

                // Deallocating the allocated memory
                free(wrappedCommand);
            }
        }
    }

    // If we had already entered any of the before cases, which was stored
    // as our flag haEnteredYet, we do not need to enter this part. Otherwise,
    // we go into this execution part
    if (!hasEnteredYet) {
        /** We don't have a background process **/
        if (indexOfBackground == -1) {
            /** No > >> >>> operations, directly execute the command **/
            if ((indexOfRedirection == -1) && (indexOfAppend == -1) && (indexOfReverseAppend == -1)) {
                numOfTotal += 1; // Increasing the total number of processes by 1
                if (isInPath(tokens[0])) {
                    // Creating args array
                    char *args[index + 1];
                    for (int b = 0; b < index + 1; b++) { args[b] = tokens[b]; }
                    // Spawn the execution
                    spawn(tokens[0], args);
                }
            }
            /** > case **/
            if (indexOfRedirection != -1) {
                if ((isInPath(tokens[0])) || (strcmp(tokens[0],"bello") == 0)) {
                    numOfTotal += 1; // Increasing the total number of processes by 1
                    // Creating args array
                    char *args[indexOfRedirection + 1];
                    for (int b = 0; b < indexOfRedirection + 1; b++) {
                        if (b != indexOfRedirection) { args[b] = tokens[b]; }
                        else { args[b] = NULL; }
                    }
                    // Spawn, but manually
                    pid_t child_pid;
                    child_pid = fork(); // Forking the parent, creating a child
                    if (child_pid != 0) {
                        // Parent enters here
                        int status; // Success-failure status of our child process
                        waitpid(child_pid,&status, 0); // Waiting for our child process to execute

                        // Check if the child process exited abnormally
                        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                            fprintf(stderr, "Child process could not be executed successfully\n"); // Setting error message
                            error = 1; // Setting error flag
                            (*reaPointer) -= 1; // Temporary decrease for the below increase case
                        }
                        (*reaPointer) += 1; // Increasing the number of reaped processes by 1 in no error case, else do not increment
                        return child_pid; // Returns the child pid
                    } else {
                        // Child enters here
                        // File redirection part
                        int file_descriptor = open(tokens[indexOfRedirection + 1], O_WRONLY | O_CREAT | O_TRUNC,
                                                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                        if (file_descriptor == -1) {
                            perror("Error opening the file");
                            error = 1;
                            exit(EXIT_FAILURE);
                        }
                        if (dup2(file_descriptor, STDOUT_FILENO) == -1) {
                            perror("Error redirecting standard output");
                            error = 1;
                            exit(EXIT_FAILURE);
                        }
                        fflush(stdout);
                        close(file_descriptor);
                        // Execution part
                        // Bello case
                        if (strcmp(tokens[0],"bello") == 0) {
                            parser("bello\0");
                            abort();
                        }
                        // Command inside path case
                        else {
                            execvp(tokens[0], args);
                            fprintf(stderr, "An error occurred in execvp\n");
                            error = 1;
                            abort();
                        }
                    }
                }
            }
            /** >> case **/
            if (indexOfAppend != -1) {
                if ((isInPath(tokens[0])) || (strcmp(tokens[0],"bello") == 0)) {
                    numOfTotal += 1; // Increasing the total number of processes by 1
                    // Creating args array
                    char *args[indexOfAppend + 1];
                    for (int b = 0; b < indexOfAppend + 1; b++) {
                        if (b != indexOfAppend) { args[b] = tokens[b]; }
                        else { args[b] = NULL; }
                    }
                    // Spawn, but manually
                    pid_t child_pid;
                    // Forking the parent, creating a child
                    child_pid = fork();
                    if (child_pid != 0) {
                        // Parent enters here
                        int status; // Success-failure status of our child process
                        waitpid(child_pid,&status, 0); // Waiting for our child process to execute

                        // Check if the child process exited abnormally
                        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                            fprintf(stderr, "Child process could not be executed successfully\n"); // Setting error message
                            error = 1; // Setting error flag
                            (*reaPointer) -= 1; // Temporary decrease for the below increase case
                        }
                        (*reaPointer) += 1; // Increasing the number of reaped processes by 1 in no error case, else do not increment
                        return child_pid; // Returns the child pid
                    } else {
                        // Child enters here
                        // File descriptor part
                        int file_descriptor = open(tokens[indexOfAppend + 1], O_WRONLY | O_CREAT | O_APPEND,
                                                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                        if (file_descriptor == -1) {
                            perror("Error opening the file");
                            error = 1;
                            exit(EXIT_FAILURE);
                        }
                        if (dup2(file_descriptor, STDOUT_FILENO) == -1) {
                            perror("Error redirecting standard output");
                            error = 1;
                            exit(EXIT_FAILURE);
                        }
                        fflush(stdout);
                        close(file_descriptor);
                        // Execution part
                        // Bello case
                        if (strcmp(tokens[0],"bello") == 0) {
                            parser("bello\0");
                            abort();
                        }
                        // Command inside the path case
                        else {
                            execvp(tokens[0], args);
                            fprintf(stderr, "An error occurred in execvp\n");
                            error = 1;
                            abort();
                        }
                    }
                }
            }
            /** >>> case **/
            if (indexOfReverseAppend != -1) {
                if ((isInPath(tokens[0])) || (strcmp(tokens[0],"bello") == 0)) {
                    numOfTotal += 1; // Increasing the total number of processes by 1
                    // Creating args array
                    char *args[indexOfReverseAppend + 1];
                    for (int b = 0; b < indexOfReverseAppend + 1; b++) {
                        if (b != indexOfReverseAppend) { args[b] = tokens[b]; }
                        else { args[b] = NULL; }
                    }
                    // Our pipe for redirections, and a pid_t
                    // 0 = read , 1 = write
                    int fd[2];
                    pid_t child_pid;
                    // Checking if the pipe has properly created
                    if (pipe(fd) == -1) {
                        fprintf(stderr, "Pipe Failed");
                        return 1;
                    }
                    // Forking the parent, creating a child
                    child_pid = fork();
                    // In case of fork error
                    if (child_pid < 0) {fprintf(stderr, "fork Failed");error=1;return 1;}

                    // Parent process
                    else if (child_pid > 0) {
                        // Parent enters here
                        int status; // Success-failure status of our child process
                        waitpid(child_pid,&status, 0); // Waiting for our child process to execute

                        // Check if the child process exited abnormally
                        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                            fprintf(stderr, "Child process could not be executed successfully\n"); // Setting error message
                            error = 1; // Setting error flag
                            (*reaPointer) -= 1; // Temporary decrease for the below increase case
                        }
                        (*reaPointer) += 1; // Increasing the number of reaped processes by 1 in no error case, else do not increment

                        // Reads the pipe and stores it in a buffer
                        close(fd[1]); // Closes the writing end of the pipe
                        /** Is this capacity enough? **/
                        char output_str[1024]; // Buffer for output string
                        ssize_t bytesRead = read(fd[0], output_str, sizeof(output_str) - 1);

                        if (bytesRead == -1) {
                            perror("Error reading from pipe");
                            error = 1;
                            exit(EXIT_FAILURE);
                        }

                        // Closes the reading end of the pipe
                        close(fd[0]);

                        // Reverses the read part from the pipe
                        char reversed_output[1024];
                        for (ssize_t iter = bytesRead-2; iter>= 0; iter--) {
                            reversed_output[bytesRead-2-iter] = output_str[iter];
                        }
                        reversed_output[bytesRead-1] = '\n';
                        reversed_output[bytesRead] = '\0'; // Null-terminate the reversed output

                        // Appends the reversed string to the destination file
                        // File descriptor part
                        int file_descriptor = open(tokens[indexOfReverseAppend + 1], O_WRONLY | O_CREAT | O_APPEND,
                                                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                        if (file_descriptor == -1) {
                            perror("Error opening the file");
                            error = 1;
                            exit(EXIT_FAILURE);
                        }

                        // Writing to the file, closing the file, exiting
                        write(file_descriptor, reversed_output, bytesRead); // Write only the actual reversed data
                        close(file_descriptor);

                        return child_pid;
                    }

                    // Child process enters here
                    else {
                        // Takes the output of the command and redirects it to the pipe
                        close(fd[0]); // Closes the reading end of the pipe
                        // Checks pipe writing errors
                        if (dup2(fd[1], STDOUT_FILENO) == -1) {
                            perror("Error redirecting standard output");
                            error = 1;
                            exit(EXIT_FAILURE);
                        }
                        // Closes the writing end of the pipe
                        close(fd[1]);

                        // Execution part
                        // Bello case
                        if (strcmp(tokens[0],"bello") == 0) {
                            parser("bello\0");
                            abort();
                        }
                        // Command inside the path case
                        else {
                            execvp(tokens[0], args);
                            fprintf(stderr, "An error occurred in execvp\n");
                            error = 1;
                            abort();
                        }
                    }
                }
            }
            return 0;
        }
        /** We have a background process **/
        else {
            /** No > >> >>> operations, directly execute the command **/
            if ((indexOfRedirection == -1) && (indexOfAppend == -1) && (indexOfReverseAppend == -1)) {
                if ((isInPath(tokens[0])) || (strcmp(tokens[0],"bello") == 0)) {
                    // Forking the parent, creating a child
                    numOfTotal += 1; // Increasing the total number of processes by 1
                    pid_t child_pid_1;
                    child_pid_1 = fork();
                    if (child_pid_1 > 0) {
                        // Parent sleeps for 1 second, then returns child_pid_1
                        sleep(1); // This part is added in order to avoid "race conditions"
                        return child_pid_1;
                    }
                    // Parent won't enter this, but child enters
                    if (child_pid_1 == 0) {
                        // Creating args array
                        char *args[index];
                        for (int b = 0; b < index; b++) {
                            if (b != index - 1) {
                                args[b] = tokens[b];
                            } else {
                                args[b] = NULL;
                            }
                        }
                        // Execution part
                        // Bello case
                        if (strcmp(tokens[0],"bello") == 0) {
                            parser("bello\0");
                            abort();
                        }
                        // Command inside the path case
                        else {
                            execvp(tokens[0], args);
                            fprintf(stderr, "An error occurred in execvp\n");
                            error = 1;
                            abort();
                        }
                    }
                }
            }
            /** > case **/
            if (indexOfRedirection != -1) {
                if ((isInPath(tokens[0])) || (strcmp(tokens[0],"bello") == 0)) {
                    // Forking the parent, creating a child
                    numOfTotal += 1; // Increasing the total number of processes by 1
                    pid_t child_pid_1;
                    child_pid_1 = fork();
                    // Parent enters
                    if (child_pid_1 > 0) {
                        // Parent sleeps for 1 second, then returns child_pid_1
                        sleep(1); // This part is added in order to avoid "race conditions"
                        return child_pid_1;
                    }
                    // Parent won't enter this, but child enters
                    if (child_pid_1 == 0) {
                        // Creating args array
                        char *args[index - 2];
                        for (int b = 0; b < index - 2; b++) {
                            if (b != index - 3) {
                                args[b] = tokens[b];
                            } else {
                                args[b] = NULL;
                            }
                        }
                        // File descriptor part (write mode)
                        int file_descriptor = open(tokens[indexOfRedirection + 1], O_WRONLY | O_CREAT | O_TRUNC,
                                                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                        if (file_descriptor == -1) {
                            perror("Error opening the file");
                            error = 1;
                            exit(EXIT_FAILURE);
                        }
                        if (dup2(file_descriptor, STDOUT_FILENO) == -1) {
                            perror("Error redirecting standard output");
                            error = 1;
                            exit(EXIT_FAILURE);
                        }
                        fflush(stdout);
                        close(file_descriptor);
                        // Execution part
                        // Bello case
                        if (strcmp(tokens[0],"bello") == 0) {
                            parser("bello\0");
                            abort();
                        }
                        // Command inside the path case
                        else {
                            execvp(tokens[0], args);
                            fprintf(stderr, "An error occurred in execvp\n");
                            error = 1;
                            abort();
                        }
                    }
                }
            }
            /** >> case **/
            if (indexOfAppend != -1) {
                if ((isInPath(tokens[0])) || (strcmp(tokens[0],"bello") == 0)) {
                    // Forking the parent, creating a child
                    numOfTotal += 1; // Increasing the total number of processes by 1
                    pid_t child_pid_1;
                    child_pid_1 = fork();
                    // Parent enters
                    if (child_pid_1 > 0) {
                        // Parent sleeps for 1 second, then returns child_pid_1
                        sleep(1); // This part is added in order to avoid "race conditions"
                        return child_pid_1;
                    }
                    // Parent won't enter this, but child enters
                    if (child_pid_1 == 0) {
                        // Creating args array
                        char *args[index - 2];
                        for (int b = 0; b < index - 2; b++) {
                            if (b != index - 3) {
                                args[b] = tokens[b];
                            } else {
                                args[b] = NULL;
                            }
                        }
                        // File descriptor stuff (append mode)
                        int file_descriptor = open(tokens[indexOfAppend + 1], O_WRONLY | O_CREAT | O_APPEND,
                                                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                        if (file_descriptor == -1) {
                            perror("Error opening the file");
                            error = 1;
                            exit(EXIT_FAILURE);
                        }
                        if (dup2(file_descriptor, STDOUT_FILENO) == -1) {
                            perror("Error redirecting standard output");
                            error = 1;
                            exit(EXIT_FAILURE);
                        }
                        fflush(stdout);
                        close(file_descriptor);
                        // Execution part
                        // Bello case
                        if (strcmp(tokens[0],"bello") == 0) {
                            parser("bello\0");
                            abort();
                        }
                        // Command inside the path case
                        else {
                            execvp(tokens[0], args);
                            fprintf(stderr, "An error occurred in execvp\n");
                            error = 1;
                            abort();
                        }
                    }
                }
            }
            /** >>> case **/
            if (indexOfReverseAppend != -1) {
                if ((isInPath(tokens[0])) || (strcmp(tokens[0],"bello") == 0)) {
                    // Forking the parent, creating a child
                    numOfTotal += 1; // Increasing the total number of processes by 1
                    pid_t child_pid_1;
                    child_pid_1 = fork();
                    // Parent enters
                    if (child_pid_1 > 0) {
                        // Parent sleeps for 1 second, then returns child_pid_1
                        sleep(1); // This part is added in order to avoid "race conditions"
                        return child_pid_1;
                    }
                    // Parent won't enter this, but child enters
                    if (child_pid_1 == 0) {
                        // Creating args array
                        char *args[indexOfReverseAppend + 1];
                        for (int b = 0; b < indexOfReverseAppend + 1; b++) {
                            if (b != indexOfReverseAppend) { args[b] = tokens[b]; }
                            else { args[b] = NULL; }
                        }
                        // Our pipe for redirections, and a pid_t
                        // 0 = read , 1 = write
                        int fd[2];
                        pid_t p;
                        // Checking if the pipe has properly created
                        if (pipe(fd) == -1) {
                            fprintf(stderr, "Pipe Failed");
                            return 1;
                        }
                        // Forking the child, creating a grandchild
                        p = fork();
                        // In case of fork error
                        if (p < 0) {fprintf(stderr, "fork Failed");error=1;return 1;}

                        // Child process (parent of grandchild)
                        else if (p > 0) {
                            // Child waits for the grandchild
                            wait(NULL);

                            // Reads the pipe and stores it in a buffer
                            close(fd[1]); // Closes the writing end of the pipe
                            char output_str[1024];
                            ssize_t bytesRead = read(fd[0], output_str, sizeof(output_str) - 1);

                            if (bytesRead == -1) {
                                perror("Error reading from pipe");
                                error = 1;
                                exit(EXIT_FAILURE);
                            }

                            close(fd[0]); // Closes the reading end of the pipe

                            // Reverses the string at the buffer
                            char reversed_output[1024];
                            for (ssize_t iter = bytesRead-2; iter>= 0; iter--) {
                                reversed_output[bytesRead-2-iter] = output_str[iter];
                            }
                            reversed_output[bytesRead-1] = '\n';
                            reversed_output[bytesRead] = '\0'; // Null-terminate the reversed output

                            // Appends the reversed string to the file
                            // File descriptor part
                            int file_descriptor = open(tokens[indexOfReverseAppend + 1], O_WRONLY | O_CREAT | O_APPEND,
                                                       S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                            if (file_descriptor == -1) {
                                perror("Error opening the file");
                                error = 1;
                                exit(EXIT_FAILURE);
                            }

                            // Writing to the file, closing the file, exiting (aborting)
                            write(file_descriptor, reversed_output, bytesRead); // Write only the actual reversed data
                            close(file_descriptor);

                            abort();
                        }

                        // Grandchild process (child of child)
                        else {
                            // Takes the output of the command (written at the left of the input), redirects it to the pipe
                            close(fd[0]); // Closes the reading end of the pipe
                            // Checks pipe writing errors
                            if (dup2(fd[1], STDOUT_FILENO) == -1) {
                                perror("Error redirecting standard output");
                                error = 1;
                                exit(EXIT_FAILURE);
                            }
                            // Closes the writing end of the pipe
                            close(fd[1]);

                            // Execution part
                            // Bello case
                            if (strcmp(tokens[0],"bello") == 0) {
                                parser("bello\0");
                                abort();
                            }
                            // Command inside the path case
                            else {
                                execvp(tokens[0], args);
                                fprintf(stderr, "An error occurred in execvp\n");
                                error = 1;
                                abort();
                            }
                        }
                        // For double-checking purposes
                        close(fd[0]); // Closes the reading end of the pipe
                        close(fd[1]); // Closes the writing end of the pipe
                    }
                }
            }
            return 0;
        }
    }
    // The end of our parser function
    return 0;
}