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
    {
        sigusr1_count++;
        printf("parent received %d SIGUSR1 signals\n", sigusr1_count);
        if (sigusr1_count >= 100)
        {
            kill(0, SIGUSR2); // Send SIGUSR2 to all child processes
        }
    }
    last_signal = sig;
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

void sigusr2_handler(int sig)
{
    exit(EXIT_SUCCESS);
}

void child_work(int i)
{
    srand(time(NULL) * getpid());
    int t = 100 + rand() % (200 - 100 + 1); // Random time between 100 and 200 milliseconds
    printf("PROCESS with pid %d chose %d ms\n", getpid(), t);
    
    struct timespec req;
    req.tv_sec = t / 1000;
    req.tv_nsec = (t % 1000) * 1000000L;

    pid_t parent_pid = getppid();

    sethandler(sigusr2_handler, SIGUSR2);

    while (1)
    {
        nanosleep(&req, NULL);
        kill(parent_pid, SIGUSR1);
    }
    
    printf("PROCESS with pid %d terminates\n", getpid());
}

void create_children(int n)
{
    pid_t s;
    for (n--; n >= 0; n--)
    {
        if ((s = fork()) < 0)
            ERR("Fork:");
        if (!s)
        {
            child_work(n);
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
    int n;
    if (argc < 2)
        usage(argv[0]);
    n = atoi(argv[1]);
    if (n <= 0)
        usage(argv[0]);

    sethandler(sig_handler, SIGUSR1);
    sethandler(sigchld_handler, SIGCHLD);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    create_children(n);

    sigprocmask(SIG_UNBLOCK, &mask, NULL);

    while (wait(NULL) > 0 || errno == EINTR)
        ;

    return EXIT_SUCCESS;
}