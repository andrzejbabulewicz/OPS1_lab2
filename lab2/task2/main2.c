#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t last_signal = 0;
volatile sig_atomic_t sigusr1_count = 0;
volatile sig_atomic_t sigusr2_received = 0;

void sethandler(void (*f)(int, siginfo_t *, void *), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_sigaction = f;
    act.sa_flags = SA_SIGINFO;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void sigusr1_handler(int sig, siginfo_t *info, void *context)
{
    if (sig == SIGUSR1)
    {        
        printf("Teacher has accepted solution of student [%d].\n", info->si_pid);
        kill(info->si_pid, SIGUSR2); // Get sender PID
        sigusr1_count++;
    }
    last_signal = sig;
}

void sigchld_handler(int sig, siginfo_t *info, void *context)
{
    pid_t pid;
    for (;;)
    {
        pid = waitpid(0, NULL, WNOHANG);
        if (pid == 0)
            return;
        if (pid <= 0)
        {
            if (errno == ECHILD)
                return;
            ERR("waitpid");
        }
    }
}

void sigusr2_handler(int sig, siginfo_t *info, void *context)
{
    sigusr2_received = 1;
}

void child_work(int i, int prob, int p, int t)
{
    sethandler(sigusr2_handler, SIGUSR2);
    int problems = 0;
    srand(time(NULL) * getpid());
    t = 100 * t;
    struct timespec req;
    req.tv_sec = t / 1000;
    req.tv_nsec = (t % 1000) * 1000000L;
    printf("Student [%d, %d] has started doing task!\n", i, getpid());
    
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR2);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
        

    for (int j = 0; j < p; j++)
    {
        
        printf("Student [%d, %d] has started doing part %d of %d!\n", i, getpid(), j + 1, p);

        for (int k = 0; k < t / 100; k++)
        {
            nanosleep(&req, NULL);
            if (rand() % 100 < prob)
            {
                struct timespec extra_req;
                extra_req.tv_sec = 0;
                extra_req.tv_nsec = 50000000L; // 50 ms
                nanosleep(&extra_req, NULL);
                printf("Student [%d, %d] has an issue (%d) doing task!\n", i, getpid(), problems + 1);
                problems++;
            }
        }

        printf("Student [%d, %d] has finished part %d of %d!\n", i, getpid(), j + 1, p);
        kill(getppid(), SIGUSR1); 

        while (!sigusr2_received)
        {
            sigsuspend(&oldmask); // Wait for SIGUSR2 from parent
        }
        sigusr2_received = 0; // Reset flag for the next part
    }

    printf("Student [%d, %d] has completed the task with %d issues!\n", i, getpid(), problems);
}

void create_children(int n, int prob, int p, int t)
{
    switch (fork())
    {
        case 0:
             // Setup SIGUSR2 handler for the child
            child_work(n, prob, p, t);
            exit(EXIT_SUCCESS);
        case -1:
            perror("Fork:");
            exit(EXIT_FAILURE);
    }
}

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s 0<n\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        usage(argv[0]);
    }

    int p = atoi(argv[1]);
    int t = atoi(argv[2]);

    sethandler(sigusr1_handler, SIGUSR1);
    //sethandler(sigchld_handler, SIGCHLD);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    for (int i = 3; i < argc; i++)
    {
        create_children(i - 3, atoi(argv[i]), p, t);
    }

    sigprocmask(SIG_UNBLOCK, &mask, NULL);

    while (wait(NULL) > 0)
    {
        ;
    }
    printf("Parent quits\n");
    fflush(stdout);
    exit(EXIT_SUCCESS);
}
