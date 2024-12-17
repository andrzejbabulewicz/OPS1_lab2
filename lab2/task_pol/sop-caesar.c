#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t last_sig = 0;
pid_t *child_pids;

ssize_t bulk_read(int fd, char* buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (c == 0)
            return len;  // EOF
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char* buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

void usage(int argc, char* argv[])
{
    printf("%s p k \n", argv[0]);
    printf("\tp - path to file to be encrypted\n");
    printf("\t0 < k < 8 - number of child processes\n");
    exit(EXIT_FAILURE);
}

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
    last_sig = sig;
}
void sigint_handler(int sig)
{
    last_sig = sig;
}

void caesar_cipher(char *text, size_t size, int shift)
{
    for (size_t i = 0; i < size; i++)
    {
        char c = text[i];
        if (c >= 'a' && c <= 'z')
        {
            text[i] = (c - 'a' + shift) % 26 + 'a';
        }
    }
}

void child_work(int fd, off_t offset, size_t size, int child_no, const char* path)
{
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    printf("PID: %d, Offset: %ld, Size: %zu\n", getpid(), offset, size);
    char* buf = malloc(size);
    if (!buf)
        ERR("malloc");

    off_t result;
    do 
    {
        result = lseek(fd, offset, SEEK_SET);
    } 
    while (result == -1 && errno == EINTR);
    
    if (result == -1)
        ERR("lseek");

    ssize_t bytes_read;
    do 
    {
        bytes_read = bulk_read(fd, buf, size);
    } 
    while (bytes_read == -1 && errno == EINTR);
    
    if (bytes_read < 0)
        ERR("read");

    while (last_sig != SIGUSR1)
    {
        sigsuspend(&oldmask);
    }

    //caesar_cipher(buf, size, 3);

    char output_filename[256];
    snprintf(output_filename, sizeof(output_filename), "%s-%d", path, child_no);
    int out_fd = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd == -1)
        ERR("open output file");

    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 100000000;    
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    for (size_t i = 0; i < size; i++)
    {
        if (buf[i] >= 'a' && buf[i] <= 'z')
        {
            buf[i] = (buf[i] - 'a' + 3) % 26 + 'a';
        }
        ssize_t bytes_written;
        do 
        {
            bytes_written = bulk_write(out_fd, &buf[i], 1);
        } 
        while (bytes_written == -1 && errno == EINTR);
        
        if (bytes_written < 0)
            ERR("write");

        sigprocmask(SIG_BLOCK, &mask, &oldmask);
        //usleep(100000);
        nanosleep(&ts, NULL); 
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
    }

    close(out_fd);
    free(buf);
    printf("PID: %d quits\n", getpid());
}



void create_children(int fd, int n, const char* path)
{
    struct stat st;
    if (fstat(fd, &st) == -1)
        ERR("fstat");

    off_t file_size = st.st_size;
    size_t part_size = file_size / n;
    size_t last_part_size = part_size + (file_size % n);

    for (int i = 0; i < n; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
            ERR("fork");
        if (pid == 0)
        {
            sethandler(sigusr1_handler, SIGUSR1);
            sethandler(sigint_handler, SIGINT);
            size_t size = (i == n - 1) ? last_part_size : part_size;
            off_t offset = i * part_size;
            child_work(fd, offset, size, i, path);
            close(fd);
            exit(EXIT_SUCCESS); // Exit child process
        }
        child_pids[i] = pid;
    }
}

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        usage(argc, argv);
    }

    char* path = argv[1];
    int k = atoi(argv[2]);

    if (k <= 0 || k >= 8)
    {
        usage(argc, argv);
    }

    child_pids = malloc(k * sizeof(pid_t));

    int fd = open(path, O_RDONLY);
    if (fd == -1)
        ERR("open");

    printf("Parent PID: %d\n", getpid());
    create_children(fd, k, path);

    sleep(1);

    for(int i = 0; i < k; i++)
    {
        kill(child_pids[i], SIGUSR1);
    }

    while (wait(NULL) > 0)
    {
        ;
    }

    free(child_pids);
    close(fd);
    printf("Parent quits\n");
    return EXIT_SUCCESS;
}

