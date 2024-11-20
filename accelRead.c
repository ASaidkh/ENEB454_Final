/*Acceloremeter reading functionality*/
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>  
#include "accelRead.h" 

// Function to open and initialize the accelerometer
int open_accel(void) {
    int fd = open(ACCEL_DEVICE_PATH, O_RDWR);

    // Check if the file opened successfully
    if (fd == -1) {
        perror("Failed to open device");
        return -1;
    }

    // Initialize the device
    if (write(fd, "init", strlen("init")) == -1) {
        perror("Failed to initialize device");
        close(fd);
        return -1;
    }

    // Calibrate the device
    if (write(fd, "calibrate", strlen("calibrate")) == -1) {
        perror("Failed to calibrate device");
        close(fd);
        return -1;
    }

    // Set the rate to 12.5 Hz
    if (write(fd, "rate 12.5", strlen("rate 12.5")) == -1) {
        perror("Failed to set rate");
        close(fd);
        return -1;
    }

    // Set resolution to 16g
    if (write(fd, "format 1 16", strlen("format 1 16")) == -1) {
        perror("Failed to set resolution");
        close(fd);
        return -1;
    }

    return fd;  // Return the file descriptor for the opened device
}

// Function to close the accelerometer driver
int close_accel(int fd) {
    if (fd != -1) {
        close(fd);  // Close the file descriptor
        return 0;  // Return success
    }
    return -1;  // Return failure if the file descriptor is invalid
}

// Function to read acceleration data and return the magnitude
int read_accel(int fd) {
    char buffer[256];
    int x, y, z, scale, deviceID;
    int magnitude;

    if (fd == -1) {
        perror("Device not open");
        return -1;  // Return error if the device is not open
    }

    // Read the data from the accelerometer
    ssize_t bytesRead = read(fd, buffer, sizeof(buffer) - 1);
    if (bytesRead == -1) {
        perror("Failed to read from device");
        return -1;  // Return error on read failure
    }

    // Null-terminate the buffer to print it as a string
    buffer[bytesRead] = '\0';

    // Parse the x, y, z, and scale values as integers
    if (sscanf(buffer, "%d %d %d %d %d", &deviceID, &x, &y, &z, &scale) == 5) {
        // Calculate the magnitude as the sum of the absolute values of x, y, and z
        magnitude = abs(x) + abs(y) + abs(z);
        return magnitude;  // Return the magnitude
    } else {
        perror("Failed to parse values from the data string");
        return -1;  // Return error if parsing fails
    }
}
