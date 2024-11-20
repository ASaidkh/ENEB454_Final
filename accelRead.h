#ifndef ACCEL_READ_H
#define ACCEL_READ_H

// Define the device path for the accelerometer
#define ACCEL_DEVICE_PATH "/dev/IntelFPGAUP/accel"

// Function to open and initialize the accelerometer, returns file descriptor
int open_accel(void);

// Function to close the accelerometer driver, accepts file descriptor
int close_accel(int fd);

// Function to read the acceleration data and return the magnitude
int read_accel(int fd);

#endif // ACCEL_READ_H
