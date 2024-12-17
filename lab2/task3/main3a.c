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

volatile sig_atomic_t keep_working = 0;
volatile sig_atomic_t terminate = 0;

void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void sig_handler(int sig)
{
    if (sig == SIGUSR1)
        keep_working = 1;
    else if (sig == SIGUSR2)
        keep_working = 0;
    else if (sig == SIGTERM)
        terminate = 1;
}

void sigchld_handler(int sig)
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

void child_work(int i)
{
    int counter = 0;
    srand(time(NULL) * getpid());
    sigset_t mask, oldmask, suspend_mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    sigemptyset(&suspend_mask);

    // Wait for the signal to start work
    while (!keep_working && !terminate)
        sigsuspend(&suspend_mask);

    while (!terminate)
    {
        if (keep_working)
        {
            // Main work loop
            int t = 100 + rand() % (200 - 100 + 1); // Random time between 100 and 200 milliseconds
            struct timespec req;
            req.tv_sec = t / 1000;
            req.tv_nsec = (t % 1000) * 1000000L;
            nanosleep(&req, NULL);
            counter++;
            printf("Child %d: Counter %d\n", i, counter);
        }
        else
        {
            // Wait for the signal to resume work
            while (!keep_working && !terminate)
                sigsuspend(&suspend_mask);
        }
    }
    printf("Child %d quits.\n", i);
}

void create_children(int n)
{
    pid_t s;
    for (int i = 0; i < n; i++)
    {
        if ((s = fork()) < 0)
            ERR("Fork:");
        if (!s)
        {
            child_work(i);
            exit(EXIT_SUCCESS);
        }
    }
}

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s 0<n\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    printf("Parent PID: %d\n", getpid());
    int n;
    if (argc < 2)
        usage(argv[0]);
    n = atoi(argv[1]);
    if (n <= 0)
        usage(argv[0]);

    sethandler(sig_handler, SIGUSR1);
    sethandler(sig_handler, SIGUSR2);
    sethandler(sig_handler, SIGTERM);
    sethandler(sigchld_handler, SIGCHLD);

    create_children(n);

    while (wait(NULL) > 0 || errno == EINTR)
        ;

    printf("parent quits\n");
    return EXIT_SUCCESS;
}