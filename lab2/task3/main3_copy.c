#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t start_work = 0;  // Controls start/resume of work
pid_t *child_pids;
int child_count;
int current_child = -1; // Tracks the current working child
volatile sig_atomic_t keep_working = 1; // Controls the child loop

void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void sigusr1_handler(int sig)
{
    // Ensure the child array is valid
    if (child_count <= 0 || child_pids == NULL)
    {
        printf("Error: No child processes available.\n");
        return;
    }

    if (current_child != -1)
    {
        // Pause the currently working child
        kill(child_pids[current_child], SIGUSR2);
        printf("Parent paused child %d (PID %d)\n", current_child, child_pids[current_child]);
    }

    // Move to the next child
    current_child = (current_child + 1) % child_count;

    // Start the next child
    kill(child_pids[current_child], SIGUSR1);
    printf("Parent started child %d (PID %d)\n", current_child, child_pids[current_child]);
}

void sigusr2_handler(int sig)
{
    start_work = 0; // Stop child from working
}

void sigusr1_child_handler(int sig)
{
    start_work = 1; // Allow child to start working
}

void sigint_child_handler(int sig)
{
    start_work = 0; // Stop child from working
    keep_working = 0; // Signal termination
}

void cleanup_children()
{
    printf("Cleaning up children...\n");
    for (int i = 0; i < child_count; i++)
    {
        if (kill(child_pids[i], SIGINT) == -1)
        {
            if (errno != ESRCH)
                ERR("kill");
        }
    }

    while (wait(NULL) > 0 || errno == EINTR)
        ; // Wait for all children to terminate
}

void sigint_handler(int sig)
{
    printf("Parent received SIGINT.\n");
    cleanup_children();
    keep_working = 0; // Signal termination
    //printf("%d\n", keep_working);
}

void child_work(int i)
{
    int counter = 0;
    srand(time(NULL) * getpid());

    sethandler(sigusr1_child_handler, SIGUSR1);
    sethandler(sigusr2_handler, SIGUSR2);
    sethandler(sigint_child_handler, SIGINT);

    printf("Child %d (PID %d) ready to start...\n", i, getpid());

    while (1)
    {
        if(keep_working == 0)
            break;
        // Wait for the signal to start or resume work
        while (!start_work)
            pause();

        // Main work loop
        while (start_work)
        {
            int t = 100 + rand() % (200 - 100 + 1); // Random time between 100 and 200 milliseconds
            struct timespec req;
            req.tv_sec = t / 1000;
            req.tv_nsec = (t % 1000) * 1000000L;
            nanosleep(&req, NULL);
            counter++;
            printf("Child %d: Counter %d\n", i, counter);
        }
        printf("Child %d paused.\n", i);
    }
    printf("Child %d quits.\n", i);
}

void create_children(int n)
{
    for (int i = 0; i < n; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
            ERR("Fork:");
        if (pid == 0)
        {
            child_work(i);
            exit(EXIT_SUCCESS); // Exit child process
        }
        child_pids[i] = pid;
    }
}

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s <number_of_children>\n", name);
    exit(EXIT_FAILURE);
}



int main(int argc, char **argv)
{
    printf("Parent PID: %d\n", getpid());
    if (argc != 2)
        usage(argv[0]);

    child_count = atoi(argv[1]);
    if (child_count <= 0)
        usage(argv[0]);

    // Allocate memory for child PIDs
    child_pids = malloc(child_count * sizeof(pid_t));
    if (child_pids == NULL)
        ERR("malloc");

    // Set the parent signal handler for SIGUSR1
    sethandler(sigusr1_handler, SIGUSR1);
    sethandler(sigint_handler, SIGINT);
    // Create child processes
    create_children(child_count);

    while (keep_working!=0)
    {
        pause();

    }

    printf("why are we not here\n");
    cleanup_children();

    free(child_pids);
    printf("Parent quits.\n");
    return EXIT_SUCCESS;
}
