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

void usage(int argc, char* argv[])
{
    printf("%s n f \n", argv[0]);
    printf("\tf - file to be processed\n");
    printf("\t0 < n < 10 - number of child processes\n");
    exit(EXIT_FAILURE);
}

void child_work(const char* content, size_t size, int child_no, const char* path)
{
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    
    while(last_sig!=SIGUSR1)
    {
        sigsuspend(&oldmask);
    }    
    
    printf("{%d}: %s\n", getpid(), content);

    char* buf = (char*)malloc(size);
    if (buf == NULL)
        ERR("malloc");

    strcpy(buf, content);
    
    char output_filename[256];
    snprintf(output_filename, sizeof(output_filename), "%s-%d", path, child_no+1);
    int out_fd = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd == -1)
        ERR("open output file");

    int to_change = 0;

    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 250000000;

    for (size_t i = 0; i < size; i++)
    {
        if(buf[i] >= 'a' && buf[i] <= 'z' || buf[i] >= 'A' && buf[i] <= 'Z')
        {
            if(to_change%2==0)
            {
                if (buf[i] >= 'a' && buf[i] <= 'z')
                {
                    buf[i] = (char)(buf[i] + 'A' - 'a');
                }
                else if (buf[i] >= 'A' && buf[i] <= 'Z')
                {
                    buf[i] = (char)(buf[i] - ('A' - 'a'));
                }
            }

            to_change++;
        }
        
        ssize_t bytes_written;
        do 
        {
            bytes_written = bulk_write(out_fd, &buf[i], 1);
        } 
        while (bytes_written == -1 && errno == EINTR);
        
        if (bytes_written < 0)
            ERR("write");

        //usleep(250000);

        nanosleep(&ts, NULL);
    }
    
    free(buf);
    close(out_fd);
    //free(content);
}

void create_children(int fd, int n, const char* path)
{
    struct stat st;
    if (fstat(fd, &st) == -1)
        ERR("fstat");

    off_t file_size = st.st_size;
    size_t part_size = file_size / n;
    size_t last_part_size = part_size + (file_size % n);

    char *file_content = (char *)malloc(file_size);
    if (file_content == NULL)
        ERR("malloc");

    if (pread(fd, file_content, file_size, 0) != file_size)
        ERR("pread");

    for (int i = 0; i < n; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
            ERR("fork");
        if (pid == 0)
        {
            sethandler(sigusr1_handler, SIGUSR1);
            size_t size = (i == n - 1) ? last_part_size : part_size;
            off_t offset = i * part_size;
            char *part_content = (char *)malloc(size);
            if (part_content == NULL)
                ERR("malloc");

            memcpy(part_content, file_content + offset, size);
            child_work(part_content, size, i, path);
            free(part_content);
            free(file_content);
            exit(EXIT_SUCCESS); // Exit child process
        }
        child_pids[i] = pid;
    }

    free(file_content);
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