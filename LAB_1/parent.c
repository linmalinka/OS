#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUFFER_SIZE 256

enum {
    RESPONSE_CONTINUE = 0,
    RESPONSE_STOP = 1
};

static int write_all(int fd, const void *buffer, size_t size)
{
    const char *ptr = buffer;

    while (size > 0) {
        ssize_t written = write(fd, ptr, size);

        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        ptr += written;
        size -= (size_t)written;
    }

    return 0;
}

static int read_all(int fd, void *buffer, size_t size)
{
    char *ptr = buffer;

    while (size > 0) {
        ssize_t count = read(fd, ptr, size);

        if (count == 0)
            return 0;

        if (count < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        ptr += count;
        size -= (size_t)count;
    }

    return 1;
}

static ssize_t read_line(int fd, char *buffer, size_t size)
{
    size_t pos = 0;

    if (size == 0)
        return -1;

    while (pos + 1 < size) {
        char c;
        ssize_t result = read(fd, &c, 1);

        if (result == 0)
            break;

        if (result < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (c == '\n')
            break;

        buffer[pos++] = c;
    }

    buffer[pos] = '\0';

    return (ssize_t)pos;
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

static void print_text(const char *text)
{
    write_all(STDOUT_FILENO, text, strlen(text));
}

static void print_error(const char *text)
{
    write_all(STDERR_FILENO, text, strlen(text));
}

int main(void)
{
    char file_name[BUFFER_SIZE];

    print_text("Enter output file name: ");

    ssize_t length = read_line(STDIN_FILENO, file_name, sizeof(file_name));

    if (length <= 0) {
        print_error("Invalid file name\n");
        return EXIT_FAILURE;
    }

    int pipe1[2];
    int pipe2[2];

    if (pipe(pipe1) == -1) {
        print_error("Failed to create pipe1\n");
        return EXIT_FAILURE;
    }

    if (pipe(pipe2) == -1) {
        print_error("Failed to create pipe2\n");

        close(pipe1[0]);
        close(pipe1[1]);

        return EXIT_FAILURE;
    }

    pid_t pid = fork();

    if (pid == -1) {
        print_error("Failed to fork\n");

        close(pipe1[0]);
        close(pipe1[1]);
        close(pipe2[0]);
        close(pipe2[1]);

        return EXIT_FAILURE;
    }

    if (pid == 0) {
        close(pipe1[1]);
        close(pipe2[0]);

        if (dup2(pipe1[0], STDIN_FILENO) == -1)
            _exit(EXIT_FAILURE);

        if (dup2(pipe2[1], STDOUT_FILENO) == -1)
            _exit(EXIT_FAILURE);

        close(pipe1[0]);
        close(pipe2[1]);

        execl("./child", "child", file_name, NULL);

        _exit(EXIT_FAILURE);
    }

    close(pipe1[0]);
    close(pipe2[1]);

    char buffer[BUFFER_SIZE];

    while (1) {
        print_text("Enter integer: ");

        length = read_line(STDIN_FILENO, buffer, sizeof(buffer));

        if (length < 0) {
            print_error("Input error\n");
            break;
        }

        if (length == 0) {
            print_error("Empty input\n");
            continue;
        }

        int value;

        if (parse_int(buffer, &value) == -1) {
            print_error("Invalid integer\n");
            continue;
        }

        if (write_all(pipe1[1], &value, sizeof(value)) == -1) {
            print_error("Failed to send value to child\n");
            break;
        }

        int response;

        int result = read_all(pipe2[0], &response, sizeof(response));

        if (result == -1) {
            print_error("Failed to read response from child\n");
            break;
        }

        if (result == 0) {
            print_error("Child terminated unexpectedly\n");
            break;
        }

        if (response == RESPONSE_STOP)
            break;
    }

    close(pipe1[1]);
    close(pipe2[0]);

    int status;

    if (waitpid(pid, &status, 0) == -1) {
        print_error("waitpid failed\n");
        return EXIT_FAILURE;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
        print_error("Child process finished with an error\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}