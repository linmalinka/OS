#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

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

static int is_prime(int value)
{
    if (value < 2)
        return 0;

    if (value == 2)
        return 1;

    if (value % 2 == 0)
        return 0;

    for (int divisor = 3;divisor <= value / divisor;
         divisor += 2) {

        if (value % divisor == 0)
            return 0;
    }

    return 1;
}

static int int_to_string(int value, char *buffer)
{
    char temp[32];
    int pos = 0;
    int length = 0;

    unsigned int number = (unsigned int)value;

    do {
        temp[pos++] = (char)('0' + number % 10);
        number /= 10;
    } while (number != 0);

    while (pos > 0)
        buffer[length++] = temp[--pos];

    buffer[length++] = '\n';

    return length;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
        return EXIT_FAILURE;

    int file_fd = open(
        argv[1],
        O_WRONLY | O_CREAT | O_TRUNC,
        0644
    );

    if (file_fd == -1)
        return EXIT_FAILURE;

    while (1) {
        int value;

        int result = read_all(
            STDIN_FILENO,
            &value,
            sizeof(value)
        );

        if (result == -1) {
            close(file_fd);
            return EXIT_FAILURE;
        }

        if (result == 0)
            break;

        int response = RESPONSE_CONTINUE;

        if (value < 0 || is_prime(value)) {
            response = RESPONSE_STOP;
        } else if (value > 1) {
            char buffer[32];

            int length = int_to_string(value, buffer);

            if (write_all(
                    file_fd,
                    buffer,
                    (size_t)length
                ) == -1) {

                close(file_fd);
                return EXIT_FAILURE;
            }
        }

        if (write_all(
                STDOUT_FILENO,
                &response,
                sizeof(response)
            ) == -1) {

            close(file_fd);
            return EXIT_FAILURE;
        }

        if (response == RESPONSE_STOP)
            break;
    }

    if (close(file_fd) == -1)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}