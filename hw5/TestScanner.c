//
// Created by abbiesarmento on 4/18/24.
//
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Replace with your actual device file path
#define DEVICE_FILE "/dev/scanner_device"

// Replace with your actual magic number and request code
#define SCANNER_MAGIC 'q'
#define SCANNER_SET_SEPARATORS _IOW(SCANNER_MAGIC, 1, char *)

// Utility function to set separators using ioctl
int set_separators(int fd, const char *separators) {
    return ioctl(fd, SCANNER_SET_SEPARATORS, separators);
}

// Utility function to read a token
ssize_t read_token(int fd, char *buffer, size_t size) {
    return read(fd, buffer, size);
}

int main() {
    int fd;
    char read_buf[1024];
    ssize_t bytes_read;

    // Open the device
    fd = open(DEVICE_FILE, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return EXIT_FAILURE;
    }

    // Set separators
    if (set_separators(fd, " \t\n") != 0) {
        perror("Failed to set separators");
        close(fd);
        return EXIT_FAILURE;
    }

    // Write data to the device
    if (write(fd, "This is a test.", 15) < 0) {
        perror("Failed to write data");
        close(fd);
        return EXIT_FAILURE;
    }

    // Read and print tokens
    while ((bytes_read = read_token(fd, read_buf, sizeof(read_buf) - 1)) > 0) {
        read_buf[bytes_read] = '\0'; // Null-terminate the string
        printf("Token: '%s'\n", read_buf);
    }

    if (bytes_read < 0) {
        perror("Failed to read data");
        close(fd);
        return EXIT_FAILURE;
    }

    // Cleanup
    close(fd);
    return EXIT_SUCCESS;
}
