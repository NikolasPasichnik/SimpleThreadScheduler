#include<ucontext.h> 
#include<pthread.h>
#include<stdio.h>
#include<stdlib.h> 
#include"queue/queue.h"
#include<stdbool.h>
#include"sut.h"
#include<fcntl.h>
#include<unistd.h>

#define STACK_SIZE 1024*1024

//Global variables for all the things in init? like pointers 
pthread_t *C_EXEC; 
pthread_t *I_EXEC; 

ucontext_t *c_exec_context; 
ucontext_t *i_exec_context; 
ucontext_t *c_child_context; 
ucontext_t *i_child_context; 

struct queue task_ready_queue;
struct queue task_wait_queue; 
int task_count = 0; 
bool shutdown_status = false; 

pthread_mutex_t ready_queue_lock; 
pthread_mutex_t wait_queue_lock; 
pthread_mutex_t task_count_lock; 
pthread_mutex_t shutdown_status_lock; 

void* c_exec_func(){
    //Setup for nanosleep 
    struct timespec sleepLength, sleepLeft; 
    sleepLength.tv_sec = 0; 
    sleepLength.tv_nsec = 100000;

    //Making new queue node to hold incoming task 
    struct queue_entry *taskNode; 

    //Keep iterating over task_ready_queue to see if there's a context 
    while (true){

        //Accessing the queue with mutex 
        pthread_mutex_lock(&ready_queue_lock); 
        taskNode = queue_pop_head(&task_ready_queue); 
        pthread_mutex_unlock(&ready_queue_lock);

        //Case where there is currently a task in the queue waiting to be run 
        if (taskNode) {
            c_child_context = taskNode->data; 
            swapcontext(c_exec_context, c_child_context);
        }

        //Case where the isn't a task in the queue
        else{   
            // Checking whether a shutdown was requested AND if all tasks have been finished - in mutex locks 
            pthread_mutex_lock(&shutdown_status_lock); 
            pthread_mutex_lock(&task_count_lock); 
            if (task_count == 0 && shutdown_status){
                pthread_mutex_unlock(&task_count_lock); 
                pthread_mutex_unlock(&shutdown_status_lock); 
                
                // Killing thread in this case
                pthread_exit(NULL);
            }
            pthread_mutex_unlock(&shutdown_status_lock); 
            pthread_mutex_unlock(&task_count_lock); 

        }


        nanosleep(&sleepLength, &sleepLeft);
    }
}

void* i_exec_func(){
    //Setup for nanosleep 
    struct timespec sleepLength, sleepLeft; 
    sleepLength.tv_sec = 0; 
    sleepLength.tv_nsec = 100000;

    //Making new queue node to hold incoming task 
    struct queue_entry *taskNode; 

    while(true){
        //Accessing the queue with mutex 
        pthread_mutex_lock(&wait_queue_lock); 
        taskNode = queue_pop_head(&task_wait_queue); 
        pthread_mutex_unlock(&wait_queue_lock);

        //Case where there is currently a task in the queue waiting to be run 
        if (taskNode) {
            i_child_context = taskNode->data; 
            swapcontext(i_exec_context, i_child_context);
        }
        // printf("woop - i\n");
        else{   

            // Checking whether a shutdown was requested AND if all tasks have been finished - in mutex locks 
            pthread_mutex_lock(&shutdown_status_lock); 
            pthread_mutex_lock(&task_count_lock); 
            if (task_count == 0 && shutdown_status){
                pthread_mutex_unlock(&task_count_lock); 
                pthread_mutex_unlock(&shutdown_status_lock); 
                // Killing thread in this case
                pthread_exit(NULL);
            }
            pthread_mutex_unlock(&shutdown_status_lock); 
            pthread_mutex_unlock(&task_count_lock); 

        }
        nanosleep(&sleepLength, &sleepLeft);
    }
    

}

//Initialize all the things required for the other function calls 
void sut_init(){
    //create C-EXEC kernel level thread 
    C_EXEC = (pthread_t *)malloc(sizeof(pthread_t)); 
    pthread_create(C_EXEC, NULL, c_exec_func, NULL); 
    c_exec_context = (ucontext_t *)malloc(sizeof(ucontext_t));
    getcontext(c_exec_context); 

    //create I-EXEC kernel level thread 
    I_EXEC = (pthread_t *)malloc(sizeof(pthread_t)); 
    pthread_create(I_EXEC, NULL, i_exec_func, NULL); 
    i_exec_context = (ucontext_t *)malloc(sizeof(ucontext_t));; 
    getcontext(i_exec_context); 

    //Create Task Ready Queue 
    task_ready_queue = queue_create();
    queue_init(&task_ready_queue); 

    //Create Task Wait Queue 
    task_wait_queue = queue_create();
    queue_init(&task_wait_queue); 

    //Initializing the mutexes
    pthread_mutex_init(&ready_queue_lock, NULL);
    pthread_mutex_init(&task_count_lock, NULL);
    pthread_mutex_init(&shutdown_status_lock, NULL);
    pthread_mutex_init(&wait_queue_lock, NULL);
}

bool sut_create(sut_task_f fn){
    //Creating a new task context
    ucontext_t *ucp; 
    ucp = (ucontext_t *)malloc(sizeof(ucontext_t)); 
    ucp->uc_stack.ss_sp = (char *)malloc(STACK_SIZE); 
    ucp->uc_stack.ss_size = STACK_SIZE; 
    ucp->uc_stack.ss_flags = 0; 
    ucp->uc_link = 0; 

    if (getcontext(ucp) == -1){
        // Unsuccessful context grab
        return false; 
    } 
    
    makecontext(ucp, fn, 0); 

    //Insert this task into the Task Ready Queue 
    pthread_mutex_lock(&ready_queue_lock);
    struct queue_entry *node = queue_new_node(ucp); 
    queue_insert_tail(&task_ready_queue, node); 
    pthread_mutex_unlock(&ready_queue_lock);

    //Incrementing the number of tasks to be done 
    pthread_mutex_lock(&task_count_lock); 
    task_count = task_count + 1; 
    pthread_mutex_unlock(&task_count_lock);

    return true; 
}

void sut_shutdown(){
    // Might need to free? i malloc'd stuff

    //Sending signal of a shutdown request - mutex lock 
    pthread_mutex_lock(&shutdown_status_lock); 
    shutdown_status = true; 
    pthread_mutex_unlock(&shutdown_status_lock);

    // Waiting on the threads to finish 
    pthread_join(*C_EXEC, NULL); 
    pthread_join(*I_EXEC, NULL); 

    //Destroying the mutexes
    pthread_mutex_destroy(&ready_queue_lock);
    pthread_mutex_destroy(&task_count_lock);
    pthread_mutex_destroy(&shutdown_status_lock);
    pthread_mutex_destroy(&wait_queue_lock); 

}

void sut_yield(){
    //Saving current context in a TCB (ucontext_t)
    // getcontext(c_child_context);

    //Storing this context in a queue node 
    struct queue_entry *taskNode = queue_new_node(c_child_context); 

    //Placing this taskNode at the back of the task ready queue 
    pthread_mutex_lock(&ready_queue_lock);
    queue_insert_tail(&task_ready_queue, taskNode); 
    pthread_mutex_unlock(&ready_queue_lock);

    //Swapping to the context of the kernel thread (C-EXEC)
    swapcontext(c_child_context, c_exec_context);
}

void sut_exit(){
    //Task was finished, we decrement the overall number of tasks to be run 
    pthread_mutex_lock(&task_count_lock); 
    task_count = task_count - 1; 
    pthread_mutex_unlock(&task_count_lock);

    //context of C-EXEC is loaded and started 
    setcontext(c_exec_context);
}


// NEW FEATURE
int sut_open(char *file_name){
    //Storing this context in a queue node 
    struct queue_entry *taskNode = queue_new_node(c_child_context); 

    // user task context is added to the back of the task_wait_queue
    pthread_mutex_lock(&wait_queue_lock);
    queue_insert_tail(&task_wait_queue, taskNode); 
    pthread_mutex_unlock(&wait_queue_lock);

    // the context of C-EXEC is swapped
    swapcontext(c_child_context, c_exec_context); 

    // int fd = fileno(filePtr); //i used to check if fileptr was nulll before
    int fd = open(file_name, O_RDWR | O_CREAT, 0777);

    //Storing this context in a queue node 
    taskNode = queue_new_node(i_child_context); 

    //Placing this taskNode at the back of the task ready queue 
    pthread_mutex_lock(&ready_queue_lock);
    queue_insert_tail(&task_ready_queue, taskNode); 
    pthread_mutex_unlock(&ready_queue_lock);

    // the context of C-EXEC is swapped
    swapcontext(i_child_context, i_exec_context);

    return fd;
}

void sut_write(int fd, char *buf, int size){
    //Storing this context in a queue node 
    struct queue_entry *taskNode = queue_new_node(c_child_context); 

    // user task context is added to the back of the task_wait_queue
    pthread_mutex_lock(&wait_queue_lock);
    queue_insert_tail(&task_wait_queue, taskNode); 
    pthread_mutex_unlock(&wait_queue_lock);

    // the context of C-EXEC is swapped
    swapcontext(c_child_context, c_exec_context);

    //writing to the file 
    write(fd, buf, size); 

    //Storing this context in a queue node 
    taskNode = queue_new_node(i_child_context); 

    //Placing this taskNode at the back of the task ready queue 
    pthread_mutex_lock(&ready_queue_lock);
    queue_insert_tail(&task_ready_queue, taskNode); 
    pthread_mutex_unlock(&ready_queue_lock);

    // the context of C-EXEC is swapped
    swapcontext(i_child_context, i_exec_context);
}


void sut_close(int fd){
    //Storing this context in a queue node 
    struct queue_entry *taskNode = queue_new_node(c_child_context); 

    // user task context is added to the back of the task_wait_queue
    pthread_mutex_lock(&wait_queue_lock);
    queue_insert_tail(&task_wait_queue, taskNode); 
    pthread_mutex_unlock(&wait_queue_lock);

    // the context of C-EXEC is swapped
    swapcontext(c_child_context, c_exec_context); 

    //closing the file 
    close(fd); 

    //Storing this context in a queue node 
    taskNode = queue_new_node(i_child_context); 

    //Placing this taskNode at the back of the task ready queue 
    pthread_mutex_lock(&ready_queue_lock);
    queue_insert_tail(&task_ready_queue, taskNode); 
    pthread_mutex_unlock(&ready_queue_lock);

    // the context of C-EXEC is swapped
    swapcontext(i_child_context, i_exec_context);
}


char* sut_read(int fd, char *buf, int size){
    //Storing this context in a queue node 
    struct queue_entry *taskNode = queue_new_node(c_child_context); 

    // user task context is added to the back of the task_wait_queue
    pthread_mutex_lock(&wait_queue_lock);
    queue_insert_tail(&task_wait_queue, taskNode); 
    pthread_mutex_unlock(&wait_queue_lock);

    // the context of C-EXEC is swapped
    swapcontext(c_child_context, c_exec_context); 
    
    read(fd, buf, size); 

    //Storing this context in a queue node 
    taskNode = queue_new_node(i_child_context); 

    //Placing this taskNode at the back of the task ready queue 
    pthread_mutex_lock(&ready_queue_lock);
    queue_insert_tail(&task_ready_queue, taskNode); 
    pthread_mutex_unlock(&ready_queue_lock);

    // the context of C-EXEC is swapped
    swapcontext(i_child_context, i_exec_context);
    
    return buf; 
}

