#include "process.h"

//--------------------------------struct----------------------------------//
typedef struct Node {
    char* directory;
    struct Node *next;
} node;

node *head = NULL;
node *current = NULL;
char *poppedDir;

//--------------------------------protoypes--------------------------------//
int process_simple(const CMD *cmdList);
void simple_localvars(const CMD *cmdList);
void simple_redin(const CMD *cmdList, int new_stdin_fd);
void simple_redinHere(const CMD *cmdList, int new_stdin_fd);
void simple_redout(const CMD *cmdList, int new_stdout_f);
void simple_redoutApp(const CMD *cmdList, int new_stdout_fd);
int process_pipe(const CMD *cmdList);
int process_sepAND(const CMD *cmdList);
int process_sepOR(const CMD *cmdList);
int process_sepEND(const CMD *cmdList, int bgflag);
int process_sequenceBG(const CMD *cmdList);
void process_background_helper(const CMD *node);
int process_cd(const CMD *cmdList);
int process_pushd(const CMD *cmdList);
int process_popd(const CMD *cmdList);
void push(char *directory);
void pop();
int printStack();
int printFromPop();
int questionMark(int status);
int processInternal(const CMD *node);

//--------------------------------------------------------------------------------------//
//--------------------------STACK (Linked List) IMPLEMENTATION--------------------------//
//insert link at the first location
void push(char *directory) 
{
   //create a link
   node *link = (node*)malloc(sizeof(node));
	
   link->directory = directory;
   //point it to old first node
   link->next = head;
   //point first to new first node
   head = link;
}

void pop()
{
    if (head != NULL)
    {
        //Move the head pointer to the next node
        node* temp = head;
        head = head->next;

        strcpy(poppedDir, temp->directory); 
        free(temp->directory);
        free(temp);
    }
}
//--------------------------------------------------------------------------------------//
int process(const CMD *cmdList)
{
    //reap zombies
    int zombieStatus; pid_t zombiePid;

    while ((zombiePid = waitpid(-1, &zombieStatus, WNOHANG)) > 0){
        fprintf(stderr, "Completed: %d (%d)\n", zombiePid, zombieStatus);
    }

    return processInternal(cmdList);
}
int processInternal(const CMD *cmdList)
{
    int type = cmdList->type;
    int status = 0;
    if (poppedDir == NULL)
        poppedDir = (char*)malloc(PATH_MAX);

    switch(type)
    {
        case SIMPLE:
        case SUBCMD:
            status = questionMark(process_simple(cmdList));
            break;

        case PIPE:
            status = questionMark(process_pipe(cmdList));
            break;

        case SEP_AND:
            status = questionMark(process_sepAND(cmdList));
            break;

        case SEP_OR:
            status = questionMark(process_sepOR(cmdList));
            break;

        case SEP_END:
            status = questionMark(process_sepEND(cmdList, 0));
            break;

        case SEP_BG:
            status = questionMark(process_sequenceBG(cmdList));
            break;

        case NONE:
            break; 
    }
    return status;
}

//-------------------------------HELPERS-----------------------------------//
int process_simple(const CMD *cmdList)
{
    pid_t pid = fork();
    
    if (pid == -1){
        perror("Error in simple");
        exit(errno);
    }

    int new_stdin_fd = 0; int new_stdout_fd = 0;

    if (pid == 0) //child
    {
        int errorStatus;
        // local variable case
        if (cmdList->nLocal > 0)
            simple_localvars(cmdList);

        //redirect in <
        if (cmdList->fromType == RED_IN)
            simple_redin(cmdList, new_stdin_fd);
            
        //redirect in to here doc <<
        if (cmdList->fromType == RED_IN_HERE)
            simple_redinHere(cmdList, new_stdin_fd);
            
        //redirect out >
        if (cmdList->toType == RED_OUT)
            simple_redout(cmdList, new_stdout_fd);

        //redirect out and append >>
        if (cmdList->toType == RED_OUT_APP)
            simple_redoutApp(cmdList, new_stdout_fd);

        //if CD
        if (cmdList->argc > 0 && strcmp(cmdList->argv[0], "cd") == 0)
        {
            errorStatus = process_cd(cmdList);
            if (errorStatus != 0){
                perror("ERROR in CD");
                exit(errorStatus);
            }
            else   
                exit(errorStatus);
        }

        //if PUSHD
        else if (cmdList->argc > 0 && (strcmp(cmdList->argv[0], "pushd") == 0))
        {
            errorStatus = process_pushd(cmdList);
            if (errorStatus != 0)
            {
                perror("ERROR in PUSHD");
                exit(errorStatus);
            }
            else
            {
                errorStatus = printStack();
                if(errorStatus != 0){
                    perror("ERROR in PUSHD");
                    exit(errorStatus);
                }
                exit(errorStatus); 
            }    
        }

        //if POP
        else if (cmdList->argc > 0 && (strcmp(cmdList->argv[0], "popd") == 0))
        {
            if (head == NULL)
            {
                perror("Cannot popd from empty stack");
                exit(0);
            }
            else
            {
                errorStatus = process_popd(cmdList);
                if (errorStatus != 0)
                {
                    perror("ERROR in POPD");
                    exit(errorStatus);
                }
                else
                {
                    errorStatus = printFromPop();
                    if(errorStatus != 0){
                        perror("ERROR in POPD");
                        exit(errorStatus);
                    }
                    exit(errorStatus); 
                }  
            }
  
        }

        else
        {
            if (cmdList->type == SIMPLE){
                //call execvp and check for error
                int exitStatus = execvp(cmdList->argv[0], cmdList->argv);
                if (exitStatus == -1) 
                {
                    perror("Error in simple");
                    exit(errno);
                }
                else
                    exit(exitStatus);
            } 
            else
                exit(processInternal(cmdList->left));
        }
    }

    else //parent
    {
        int child_status; 
        signal(SIGINT, SIG_IGN);
        waitpid(pid, &child_status, 0);
        signal(SIGINT, SIG_DFL);

        if ((cmdList->argc > 0) && (strcmp(cmdList->argv[0], "cd") == 0))
        {
            int errorStatus = process_cd(cmdList);
            if (errorStatus != 0)
                return errorStatus;
            else
                return 0;
        }

        else if (cmdList->argc > 0 && (strcmp(cmdList->argv[0], "pushd") == 0))
        {
            int errorStatus = process_pushd(cmdList);
            if (errorStatus != 0)
                return errorStatus;
            else  
                return 0;
        }

        else if (cmdList->argc > 0 && (strcmp(cmdList->argv[0], "popd") == 0))
        {   
            int errorStatus = process_popd(cmdList);
            if (errorStatus != 0)
                return errorStatus;
            else  
                return 0;
        }
        
        return STATUS(child_status);
    }
}

void simple_localvars(const CMD *cmdList)
{
    int len = cmdList->nLocal;
    for (int i = 0; i < len; i++)
        setenv(cmdList->locVar[i], cmdList->locVal[i], 1);
}

void simple_redin(const CMD *cmdList, int new_stdin_fd)
{
    if ((new_stdin_fd = open(cmdList->fromFile, O_RDONLY, 0664)) == -1){
        perror("Error in input redirection");
        exit(errno);
    }
    dup2(new_stdin_fd, STDIN_FILENO);
    close(new_stdin_fd);
}

void simple_redinHere(const CMD *cmdList, int new_stdin_fd)
{
    char template[16] = "/tmp/fileXXXXXX";
    int len = strlen(cmdList->fromFile);

    new_stdin_fd = mkstemp(template);

    write(new_stdin_fd, cmdList->fromFile, len);
    lseek(new_stdin_fd, 0, SEEK_SET);
    dup2(new_stdin_fd, STDIN_FILENO); 
    unlink(template);

    close(new_stdin_fd);
}

void simple_redout(const CMD *cmdList, int new_stdout_fd)
{
    if ((new_stdout_fd = open(cmdList->toFile, O_WRONLY|O_CREAT|O_TRUNC, 0664)) == -1){
        perror("Error in output redirection");
        exit(errno);
    }
    dup2(new_stdout_fd, STDOUT_FILENO);
    close(new_stdout_fd);
}

void simple_redoutApp(const CMD *cmdList, int new_stdout_fd)
{
    if ((new_stdout_fd = open(cmdList->toFile, O_WRONLY|O_CREAT|O_APPEND, 0664)) == -1){
        perror("Error in output append");
        exit(errno);
    }
    dup2(new_stdout_fd, STDOUT_FILENO);
    close(new_stdout_fd);
}

int process_pipe(const CMD *cmdList)
{
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        perror("Error in pipe");
        exit(errno);
    } //error check here
    
    pid_t pidLeft = fork();
    if (pidLeft == -1){
        perror("Error in pipe");
        exit(errno);
    }

    //handle left child
    if (pidLeft == 0)
    {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        exit(processInternal(cmdList->left));
    }

    pid_t pidRight = fork();
    if (pidRight == -1){
        perror("Error in pipe");
        exit(errno);
    }
        
    if (pidRight == 0)
    {
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        exit(processInternal(cmdList->right));
    }

    else
    {   
        int child_status_left;
        int child_status_right;
        close(pipefd[0]);
        close(pipefd[1]);
        signal(SIGINT, SIG_IGN);
        waitpid(pidRight, &child_status_right, 0);
        waitpid(pidLeft, &child_status_left, 0);
        signal(SIGINT, SIG_DFL);
        if (STATUS(child_status_right != 0))
            return STATUS(child_status_right);
        else
            return STATUS(child_status_left);
    }
}

int process_sepAND(const CMD *cmdList)
{
    int exitStatus = processInternal(cmdList->left);
    if (exitStatus == 0)
        return processInternal(cmdList->right);
    else 
        return exitStatus;
}

int process_sepOR(const CMD *cmdList)
{
    int exitStatus = processInternal(cmdList->left);
    if (exitStatus == 0)
        return exitStatus;
    else 
        return processInternal(cmdList->right);
}

int process_sepEND(const CMD *cmdList, int bgflag)
{
    //process left node, then process right node
    processInternal(cmdList->left);
    return processInternal(cmdList->right);
}

int process_sequenceBG(const CMD *cmdList)
{
    CMD *left = cmdList->left;
    CMD *right = cmdList->right;

    if (left->type == SEP_END)
    {
        processInternal(left->left);
        process_background_helper(left->right);
    }

    else if (left->type == SEP_BG)
    {
        process_background_helper(left->left);
        process_background_helper(left->right);
    }

    else if (left->type != SEP_END && left->type != SEP_BG)
        process_background_helper(left);

    if (right != NULL)
        return processInternal(right);
    
    return 0;
}

void process_background_helper(const CMD *node)
{
    if (node != NULL)
    {
        if (node->type == SEP_END)
        {
            processInternal(node->left);
            process_background_helper(node->right);
        }

        else if (node->type == SEP_BG)
        {
            process_sequenceBG(node->left);
            process_sequenceBG(node->right);
        }

        else //do background on node this is the base case essentially
        {
            pid_t pidBG = fork();
            
            if(pidBG == -1){
                perror("Error in background");
                exit(errno);
            }

            if (pidBG == 0){
                exit(processInternal(node));
            }
            else{
                fprintf(stderr, "Backgrounded: %d\n", pidBG);
            }
        }
    }
}

int process_cd(const CMD* cmdList)
{
    //error checking
    int argNum = cmdList->argc;
    
    if (argNum == 1){ 
        if (chdir(getenv("HOME")) == -1)
            return errno;     
    }

    else if (argNum > 2){
        return 1;
    }

    else if (chdir(cmdList->argv[1]) == -1){
        return errno;
    }

    return 0;
}

int process_pushd(const CMD *cmdList)
{
    if (cmdList->argc == 1 || cmdList->argc > 2)
        return 1;

    else
    {
        char *currDir = malloc(PATH_MAX);
        if (getcwd(currDir, PATH_MAX) == NULL)
            return errno;
        else
        {
            push(currDir);
            if (chdir(cmdList->argv[1]) == -1)
                return errno; 
        }
        return 0;
    }
}

int printStack()
{
    char *dirName = malloc(PATH_MAX);

    if (getcwd(dirName, PATH_MAX) == NULL)
        return errno;
    else
        fprintf(stdout, "%s", dirName);

    node *ptr = head;

    while(ptr != NULL)
    {
        fprintf(stdout, " %s", ptr->directory);
        ptr = ptr->next;
    }
    fprintf(stdout, "\n");

    free(dirName);
    return 0;
}

int process_popd(const CMD *cmdList)
{
    if (cmdList->argc == 1)
    {
        pop();
        if (chdir(poppedDir) == -1)
            return errno; 
        return 0;
    }
    else
        return 1;
}

int printFromPop()
{
    if (getcwd(poppedDir, PATH_MAX) == NULL)
        return errno;
    else
        fprintf(stdout, "%s", poppedDir);

    node *ptr = head;

    while(ptr != NULL)
    {
        fprintf(stdout, " %s", ptr->directory);
        ptr = ptr->next;
    }
    fprintf(stdout, "\n");
    return 0;
}

int questionMark(int status)
{
    char str_status[4];
    sprintf(str_status, "%d", status);
    setenv("?", str_status, 1);
    return status;
}
