#define _POSIX_C_SOURCE 200809L
#include "ipc.h"
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define BUFFER_SIZE 256

static int read_line(char *buffer, size_t capacity)
{
    size_t used = 0;
    int too_long = 0;
    for (;;) {
        char c;
        ssize_t result = read(STDIN_FILENO, &c, 1);
        if (result < 0 && errno == EINTR)
            continue;
        if (result < 0)
            return -1;
        if (result == 0 && used == 0)
            return 0;
        if (result == 0 || c == '\n')
            break;
        if (used + 1 < capacity)
            buffer[used++] = c;
        else
            too_long = 1;
    }
    buffer[used] = '\0';
    return too_long ? -2 : 1;
}

static int parse_int(const char *str, int *value)
{
    char *end;
    long number;

    errno = 0;
    number = strtol(str, &end, 10);

    if (str == end)
        return -1;

    while (*end == ' ' || *end == '\t')
        end++;

    if (*end != '\0')
        return -1;

    if (errno == ERANGE || number < INT_MIN || number > INT_MAX)
        return -1;

    *value = (int)number;
    return 0;
}

static void make_name(char *name, char kind)
{
    unsigned long pid = (unsigned long)getpid();
    char digits[sizeof(pid) * CHAR_BIT];
    size_t length = 0;
    do {
        digits[length++] = (char)('0' + pid % 10);
        pid /= 10;
    } while (pid != 0);
    memcpy(name, "/oslab3-", 8);
    name += 8;
    *name++ = kind;
    while (length > 0)
        *name++ = digits[--length];
    *name = '\0';
}

static int receive(sem_t *reply, pid_t *child)
{
    while (wait_tick(reply) == -1) {
        if (errno != ETIMEDOUT) {
            report("sem_timedwait");
            return -1;
        }
        int status;
        pid_t result = waitpid(*child, &status, WNOHANG);
        if (result == *child) {
            *child = -1;
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && sem_trywait(reply) == 0)
                return 0;
            say(STDERR_FILENO, "Child exited before replying\n");
            return -1;
        }
        if (result == -1 && errno != EINTR) {
            report("waitpid");
            return -1;
        }
    }
    return 0;
}

int main(void)
{
    char file_name[BUFFER_SIZE], line[BUFFER_SIZE];
    say(STDOUT_FILENO, "Enter output file name: ");
    if (read_line(file_name, sizeof(file_name)) != 1 || file_name[0] == '\0') {
        say(STDERR_FILENO, "Invalid or unreadable file name\n");
        return EXIT_FAILURE;
    }

    char memory_name[10 + sizeof(unsigned long) * CHAR_BIT];
    char request_name[sizeof(memory_name)], reply_name[sizeof(memory_name)];
    make_name(memory_name, 'm');
    make_name(request_name, 'q');
    make_name(reply_name, 'r');

    int status = EXIT_FAILURE;
    pid_t child = -1;
    sem_t *request = SEM_FAILED, *reply = SEM_FAILED;
    Mailbox *box = MAP_FAILED;
    int fd = shm_open(memory_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1) {
        report("shm_open");
        return status;
    }
    int memory_named = 1, request_named = 0, reply_named = 0;

    if (ftruncate(fd, sizeof(*box)) == -1) {
        report("ftruncate");
        goto cleanup;
    }
    box = mmap(NULL, sizeof(*box), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (box == MAP_FAILED) {
        report("mmap");
        goto cleanup;
    }
    request = sem_open(request_name, O_CREAT | O_EXCL, 0600, 0);
    if (request == SEM_FAILED) {
        report("sem_open request");
        goto cleanup;
    }
    request_named = 1;
    reply = sem_open(reply_name, O_CREAT | O_EXCL, 0600, 0);
    if (reply == SEM_FAILED) {
        report("sem_open reply");
        goto cleanup;
    }

    reply_named = 1;
    child = fork();
    if (child == -1) {
        report("fork");
        goto cleanup;
    }
    if (child == 0) {
        execl("./child", "child", file_name, memory_name, request_name, reply_name,
              (char *)NULL);
        report("execl ./child");
        _exit(EXIT_FAILURE);
    }
    if (receive(reply, &child) == -1 || box->response != READY)
        goto cleanup;

    /* Both processes hold their handles now. Names can be removed safely. */
    if (shm_unlink(memory_name) == -1) {
        report("shm_unlink");
        goto cleanup;
    }
    memory_named = 0;
    if (sem_unlink(request_name) == -1) {
        report("sem_unlink request");
        goto cleanup;
    }
    request_named = 0;
    if (sem_unlink(reply_name) == -1) {
        report("sem_unlink reply");
        goto cleanup;
    }
    reply_named = 0;

    for (;;) {
        say(STDOUT_FILENO, "Enter integer: ");
        int result = read_line(line, sizeof(line));
        if (result == -1) {
            report("read");
            break;
        }
        int value = 0;
        if (result != 0 && (result == -2 || parse_int(line, &value) == -1)) {
            say(STDERR_FILENO, "Invalid integer\n");
            continue;
        }
        box->value = value;
        box->eof = result == 0;
        if (sem_post(request) == -1) {
            report("sem_post request");
            break;
        }
        if (receive(reply, &child) == -1 || box->response == FAILED)
            break;
        if (box->response == STOP) {
            status = EXIT_SUCCESS;
            break;
        }
    }

cleanup:
    if (child > 0) {
        if (status != EXIT_SUCCESS && kill(child, SIGKILL) == -1 && errno != ESRCH)
            report("kill child");
        int child_status;
        pid_t result;
        do {
            result = waitpid(child, &child_status, 0);
        } while (result == -1 && errno == EINTR);
        if (result == -1)
            report("waitpid");
        if (result == -1 || !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
            status = EXIT_FAILURE;
    }
    if (memory_named)
        checked_cleanup(shm_unlink(memory_name), "shm_unlink", &status);
    if (request_named)
        checked_cleanup(sem_unlink(request_name), "sem_unlink request", &status);
    if (reply_named)
        checked_cleanup(sem_unlink(reply_name), "sem_unlink reply", &status);
    if (request != SEM_FAILED)
        checked_cleanup(sem_close(request), "sem_close request", &status);
    if (reply != SEM_FAILED)
        checked_cleanup(sem_close(reply), "sem_close reply", &status);
    if (box != MAP_FAILED)
        checked_cleanup(munmap(box, sizeof(*box)), "munmap", &status);
    checked_cleanup(close(fd), "close shared memory", &status);
    return status;
}
