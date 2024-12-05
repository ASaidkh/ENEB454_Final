// VGA video character driver with clear, pixel, line, sync, box, erase, and text commands. Sync has buffer swap between ONCHIP and SDRAM. Character and pixel writing.
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "address_map_arm.h"
#include "pixelArrays.h"

// Declare global variables needed to use the pixel buffer
void *LW_virtual, *SDRAM_virtual, *ONCHIP_virtual, *FPGA_CHAR_virtual;                   // used to access FPGA light-weight bridge
volatile int *pixel_ctrl_ptr;       // virtual address of pixel buffer controller and character buffer
int pixel_buffer, character_buffer;                   // used for virtual address of pixel buffer
int resolution_x, resolution_y;     // VGA screen size
int char_resolution_x, char_resolution_y;

// Declare variables and prototypes needed for a character device driver
dev_t dev_num;
static struct cdev video_cdev;
static struct class *video_class = NULL;
static struct task_struct *video_thread;

#define DEVICE_NAME "video"
#define BUF_SIZE 100
#define TOP_CUTOFF 60

// Function Prototypes

// Device driver utilities
static int video_open(struct inode *inode, struct file *file);
static int video_close(struct inode *inode, struct file *file);
static ssize_t video_read(struct file *file, char __user *buf, size_t len, loff_t *offset);
static ssize_t device_write(struct file *filp, const char *buffer, size_t length, loff_t *offset);

// Standard video driver functionality
void get_screen_specs(volatile int *pixel_ctrl_ptr);
void clear_screen(void);
void plot_pixel(int x, int y, short int color);
void draw_line(int x0, int y0, int x1, int y1, short int color);
void sync_with_vga(void);   
void draw_box(int x0, int y0, int x1, int y1, short int color);
void write_char(int x, int y, char c);
void write_string(int x, int y, const char *str);
void erase(void);

// Obstacle drawing functions
void draw_DogRun1(int x, int y);
void draw_DogRun2(int x, int y);
void draw_DogRun3(int x, int y);
void draw_Dog_Crouch(int x, int y);
void draw_Cat(int x, int y);
void draw_Mushroom(int x, int y);
void draw_Crystal(int x, int y);
void draw_Grass(int x, int y);
void draw_Pond(int x, int y);
void draw_TopBackground(void);


// File operations structure
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = video_open,
    .release = video_close,
    .read = video_read,
    .write = device_write,
};

// Initialize the video driver
static int __init start_video(void)
{
    
    int result;
    char_resolution_x = 80; // Default character buffer resolution
    char_resolution_y = 60;

    // Allocate device number
    result = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (result < 0) {
        printk(KERN_ERR "Failed to allocate a device number\n");
        return result;
    }

    // Initialize the cdev structure and add it to the kernel
    cdev_init(&video_cdev, &fops);
    video_cdev.owner = THIS_MODULE;
    result = cdev_add(&video_cdev, dev_num, 1);
    if (result < 0) {
        unregister_chrdev_region(dev_num, 1);
        printk(KERN_ERR "Failed to add cdev\n");
        return result;
    }

    // Create device class
    video_class = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(video_class)) {
        cdev_del(&video_cdev);
        unregister_chrdev_region(dev_num, 1);
        printk(KERN_ERR "Failed to create class\n");
        return PTR_ERR(video_class);
    }

    // Create device
    if (device_create(video_class, NULL, dev_num, NULL, DEVICE_NAME) == NULL) {
        class_destroy(video_class);
        cdev_del(&video_cdev);
        unregister_chrdev_region(dev_num, 1);
        printk(KERN_ERR "Failed to create device\n");
        return -1;
    }

    // Map FPGA lightweight bridge
    LW_virtual = ioremap_nocache(LW_BRIDGE_BASE, LW_BRIDGE_SPAN);
    if (LW_virtual == NULL) {
        printk(KERN_ERR "Error: ioremap_nocache returned NULL for LW buffer\n");
        return -ENOMEM;
    }

    // Map SDRAM
    SDRAM_virtual = ioremap_nocache(SDRAM_BASE, SDRAM_SPAN);
    if (SDRAM_virtual == NULL) {
        printk(KERN_ERR "Error: ioremap_nocache returned NULL for SDRAM buffer\n");
        return -ENOMEM;
    }

    // Map ONCHIP pixel buffer
    ONCHIP_virtual = ioremap_nocache(FPGA_ONCHIP_BASE, FPGA_ONCHIP_SPAN);
    if (ONCHIP_virtual == NULL) {
        printk(KERN_ERR "Error: ioremap_nocache returned NULL for ONCHIP buffer\n");
        return -ENOMEM;
    }

    // Map character buffer
    FPGA_CHAR_virtual = ioremap_nocache(FPGA_CHAR_BASE, FPGA_CHAR_SPAN);
    if (FPGA_CHAR_virtual == NULL) {
        printk(KERN_ERR "Error: ioremap_nocache returned NULL for CHARACTER buffer\n");
        return -ENOMEM;
    }

   

    // Create virtual memory access to the pixel buffer controller
    pixel_ctrl_ptr = (volatile int *)(LW_virtual + PIXEL_BUF_CTRL_BASE);
    get_screen_specs(pixel_ctrl_ptr);

    // Create virtual memory access to the pixel buffer
    pixel_buffer = (int)SDRAM_virtual;
    if (pixel_buffer == 0) {
        printk(KERN_ERR "Error: ioremap_nocache returned NULL\n");
        return -ENOMEM;
    }

    // Create virtual memory access to the character buffer
    character_buffer = (int)FPGA_CHAR_virtual;
    if (character_buffer == 0) {
        printk(KERN_ERR "Error: ioremap_nocache returned NULL for CHAR BUFFER\n");
        return -ENOMEM;
    }

    // Erase the pixel buffer
    clear_screen();
    
    printk(KERN_INFO "Video driver started successfully\n");
    return 0;
}

// Exit the video driver
static void __exit stop_video(void)
{
    clear_screen();

    // Unmap the virtual addresses
    iounmap(LW_virtual);
    iounmap((void *)pixel_buffer);

    // Destroy device and class
    device_destroy(video_class, dev_num);
    class_destroy(video_class);

    // Delete cdev and unregister device number
    cdev_del(&video_cdev);
    unregister_chrdev_region(dev_num, 1);

    printk(KERN_INFO "Video driver removed\n");
}

// Function to synchronize with VGA controller
void sync_with_vga(void)
{
    volatile int status = 1;
    *pixel_ctrl_ptr = 1;  // Write 1 to the Buffer register to start a swap
    
    // Wait until the S bit in the Status register is cleared
    while ((status & 0x01) != 0) {
        status = *(pixel_ctrl_ptr + 3);
    }

    if ( *(pixel_ctrl_ptr + 1) == SDRAM_BASE)
        pixel_buffer = (int) SDRAM_virtual;
    else
        pixel_buffer = (int) ONCHIP_virtual;
}

// Function to open the device
static int video_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "Video driver opened\n");
    return 0;
}

// Function to close the device
static int video_close(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "Video driver closed\n");
    return 0;
}

// Function to read from the device
static ssize_t video_read(struct file *file, char __user *buf, size_t len, loff_t *offset)
{
    char buffer[BUF_SIZE];
    int bytes_read;

    snprintf(buffer, BUF_SIZE, "%d %d\n", resolution_x, resolution_y);
    bytes_read = strlen(buffer) + 1;

    if (*offset >= bytes_read)
        return 0;

    if (len > bytes_read - *offset)
        len = bytes_read - *offset;

    if (copy_to_user(buf, buffer + *offset, len))
        return -EFAULT;

    *offset += len;
    return len;
}

// Function to write to the device
static ssize_t device_write(struct file *filp, const char *buffer, size_t length, loff_t *offset) {
    char *command;
    int x, y, x0, y0, x1, y1, num;
    short int color;
    char text[256];

    command = kmalloc(length + 1, GFP_KERNEL);
    if (command == NULL) {
        printk(KERN_ERR "Error allocating memory\n");
        return -ENOMEM;
    }

    if (copy_from_user(command, buffer, length)) {
        kfree(command);
        return -EFAULT;
    }
    command[length] = '\0';

    // Clear screen command
    if (strncmp(command, "clear", 5) == 0) {
        clear_screen();
    }
    // Command to draw running dog (frame 1)
    else if (sscanf(command, "DogRun1 %d,%d", &x, &y) == 2) {
        draw_DogRun1(x, y);
    }
    // Command to draw running dog (frame 2)
    else if (sscanf(command, "DogRun2 %d,%d", &x, &y) == 2) {
        draw_DogRun2(x, y);
    }
    // Command to draw running dog (frame 3)
    else if (sscanf(command, "DogRun3 %d,%d", &x, &y) == 2) {
        draw_DogRun3(x, y);
    }
    // Command to draw crouching dog(frame 4)
    else if (sscanf(command, "DogCrouch %d,%d", &x, &y) == 2) {
        draw_Dog_Crouch(x, y);
    }
    // Command to draw a cat
    else if (sscanf(command, "Cat %d,%d", &x, &y) == 2) {
        draw_Cat(x, y);
    }
    // Command to draw a mushroom
    else if (sscanf(command, "Mushroom %d,%d", &x, &y) == 2) {
        draw_Mushroom(x, y);
    }
    // Command to draw a crystal
    else if (sscanf(command, "Crystal %d,%d", &x, &y) == 2) {
        draw_Crystal(x, y);
    }
    // Command to draw Grass
    else if (sscanf(command, "Grass %d,%d", &x, &y) == 2) {
        draw_Grass(x, y);
    }
     // Command to draw a pond 
    else if (sscanf(command, "Pond %d,%d", &x, &y) == 2) {
        draw_Pond(x, y);
    }
    // Clear character buffer command
    else if (strncmp(command, "erase", 5) == 0) {
        erase();
    }
    // Command to draw a single pixel, e.g., "pixel 100,100 0x07E0"
    else if (sscanf(command, "pixel %d,%d %hx", &x, &y, &color) == 3) {
        plot_pixel(x, y, color);
    }
    // Command to draw a line, e.g., "line 0,0 100,100 0xFFFF"
    else if (sscanf(command, "line %d,%d %d,%d %hx", &x0, &y0, &x1, &y1, &color) == 5) {
        draw_line(x0, y0, x1, y1, color);
    }
    // Command to draw a box, e.g., "box 10,10 20,20 0xF800"
    else if (sscanf(command, "box %d,%d %d,%d %hx", &x0, &y0, &x1, &y1, &color) == 5) {
        draw_box(x0, y0, x1, y1, color);
    }
    // Command to write text, e.g., "text 10,10 Hello World"
    else if (sscanf(command, "text %d,%d %n", &x, &y, &num) == 2) {
        strncpy(text, command + num, sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
        write_string(x, y, text);
    }
    else if(strncmp(command, "TopBackground", 13) == 0) {
        draw_TopBackground();
    }
    // Sync command
    else if (strncmp(command, "sync", 4) == 0) {
        sync_with_vga(); // Sync command to synchronize with VGA
    }
    // Handle invalid commands
    else {
        printk(KERN_WARNING "Invalid command: %s\n", command);
        kfree(command);
        return -EINVAL;
    }

    kfree(command);
    return length;
}

// Function to get screen specifications
void get_screen_specs(volatile int *pixel_ctrl_ptr)
{
    int resolution_reg = *(pixel_ctrl_ptr + 2); // Read the Resolution register
    resolution_x = resolution_reg & 0xFFFF;
    resolution_y = (resolution_reg >> 16) & 0xFFFF;
}

// Function to clear the screen (set all pixels to black)
void clear_screen(void)
{
    int x, y;
    for (y = TOP_CUTOFF; y < resolution_y; y++) {
        for (x = 0; x < resolution_x; x++) {
            plot_pixel(x, y, 0x2D9D); // Set pixel to light blue
        }
    }
}

// Function to plot a pixel at (x, y) with color
void plot_pixel(int x, int y, short int color)
{
    volatile short int *pixel_address;
    pixel_address = (volatile short int *)(pixel_buffer + (y << 10) + (x << 1));
    *pixel_address = color;
}

void draw_line(int x0, int y0, int x1, int y1, short int color)
{
    int deltax, deltay, error, y, y_step;
    int is_steep = (abs(y1 - y0) > abs(x1 - x0));
    if (is_steep) {
        // Swap x and y coordinates if the line is steep
        int temp = x0; x0 = y0; y0 = temp;
        temp = x1; x1 = y1; y1 = temp;
    }

    if (x0 > x1) {
        // Swap start and end points if x0 > x1
        int temp = x0; x0 = x1; x1 = temp;
        temp = y0; y0 = y1; y1 = temp;
    }

    deltax = (x1 - x0);
    deltay = abs(y1 - y0);
    error = -(deltax / 2);
    y = y0;
    y_step = (y0 < y1) ? 1 : -1;

  

    for (; x0 <= x1; x0++) {
        if (is_steep) {
            plot_pixel(y, x0, color);  // Plot the pixel with swapped coordinates
        } else {
            plot_pixel(x0, y, color);
        }
        error += deltay;
        if (error >= 0) {
            y += y_step;
            error -= deltax;
        }
    }
}

void draw_box(int x0, int y0, int x1, int y1, short int color)
{
    int x, y;
    // Loop over the rectangle's area and fill it with the specified color
    y = y0;
    for (; y <= y1; y++) {
        x = x0;
        for (; x <= x1; x++) {
            plot_pixel(x, y, color);
        }
    }
}

// Function to write a character to the character buffer
void write_char(int x, int y, char c) {
    if (x < 0 || x >= char_resolution_x || y < 0 || y >= char_resolution_y)
        return;
    *(volatile char *)(character_buffer + (y << 7) + x) = c;
}

// Function to write a string starting at (x, y)
void write_string(int x, int y, const char *str) {
    while (*str) {
        write_char(x++, y, *str++);
        if (x >= char_resolution_x) {
            x = 0;
            y++;
            if (y >= char_resolution_y)
                break;
        }
    }
}

// Function to erase all text on the screen
void erase()
{
    int x, y;
    for (y = 0; y < 60; y++) {  // Adjust to character grid resolution
        for (x = 0; x < 80; x++) {
            volatile short int *character_address = (volatile short int *)(character_buffer + (y << 7) + x);
            *character_address = (int)' ';  // Clear the character buffer by setting to a blank space
        }
    }
}

// Function to draw running dog (frame 1)
void draw_DogRun1(int x, int y) {
    int i,j;
    // Loop through DogRun1 pixel array and draw pixels
    for (i = 0; i < DOGRUN_HEIGHT; ++i) {
        for (j = 0; j < DOGRUN_WIDTH; ++j) {
            if (x + j >= 0 && DogRun1[i][j] != 0x2D9D) {
                plot_pixel( x + j, y + i, DogRun1[i][j]);
            }
        }
    }
}

// Function to draw running dog (frame 2)
void draw_DogRun2(int x, int y) {
    int i,j;
    // Loop through DogRun2 pixel array and draw pixels
    for (i = 0; i < DOGRUN_HEIGHT; ++i) {
        for (j = 0; j < DOGRUN_WIDTH; ++j) {
            if (x + j >= 0 && DogRun2[i][j] != 0x2D9D) {
                plot_pixel( x + j, y + i, DogRun2[i][j]);
            }
        }
    }
}

// Function to draw running dog (frame 3)
void draw_DogRun3(int x, int y) {
    int i,j;
    // Loop through DogRun3 pixel array and draw pixels
    for (i = 0; i < DOGRUN_HEIGHT; ++i) {
        for (j = 0; j < DOGRUN_WIDTH; ++j) {
            if (x + j >= 0 && DogRun3[i][j] != 0x2D9D) {
                plot_pixel( x + j, y + i, DogRun3[i][j]);
            }
        }
    }
}
// Function to draw crouching dog
void draw_Dog_Crouch(int x, int y) {
    int i,j;
    // Loop through Dog_Crouch pixel array and draw pixels
    for (i = 0; i < DOG_CROUCH_HEIGHT; ++i) {
        for (j = 0; j < DOG_CROUCH_WIDTH; ++j) {
             if (x + j >= 0 && Dog_Crouch[i][j] != 0x2D9D) {
                plot_pixel( x + j, y + i, Dog_Crouch[i][j]);
             }
        }
    }
}

// Function to draw cat obstacle
void draw_Cat(int x, int y) {
    int i,j;
    // Loop through Cat pixel array and draw pixels
    for (i = 0; i < CAT_HEIGHT; ++i) {
        for (j = 0; j < CAT_WIDTH; ++j) {
             if (x + j >= 0 && Cat[i][j] != 0x2D9D) {
                plot_pixel( x + j, y + i, Cat[i][j]);
             }
        }
    }
}

// Function to draw mushroom obstacle 
void draw_Mushroom(int x, int y) {
    
    int i,j;
    // Loop through Mushroom pixel array and draw pixels
    for (i = 0; i < MUSHROOM_HEIGHT; ++i) {
        for (j = 0; j < MUSHROOM_WIDTH; ++j) {
             if (x + j >= 0 && Mushroom[i][j] != 0x2D9D) {
             plot_pixel( x + j, y + i, Mushroom[i][j]);
             }
        }
    }
}

// Function to draw crystal obstacle 
void draw_Crystal(int x, int y) {
    
    int i,j;
    // Loop through Crystal pixel array and draw pixels
    for (i = TOP_CUTOFF; i < CRYSTAL_HEIGHT; ++i) {
        for (j = 0; j < CRYSTAL_WIDTH; ++j) {
            if (x + j >= 0 && Crystal[i][j] != 0x2D9D) {
                plot_pixel( x + j, y + i, Crystal[i][j]);
            }
             
        }
    }
}

// Function to draw grass
void draw_Grass(int x, int y) {
    
    int i,j;
    // Loop through Crystal pixel array and draw pixels
    for (i = 0; i < GRASS_HEIGHT; ++i) {
        for (j = 0; j < GRASS_WIDTH; ++j) {
             plot_pixel( x + j, y + i, Grass[i][j]);
        }
    }
}

// Function to draw Pond obstacle
void draw_Pond(int x, int y) {
    
    int i,j;
    // Loop through Crystal pixel array and draw pixels
    for (i = 0; i < POND_HEIGHT; ++i) {
        for (j = 0; j < POND_WIDTH; ++j) {
             plot_pixel( x + j, y + i, Pond[i][j]);
        }
    }
}

void draw_TopBackground(){
    int i,j;
    // Loop through Crystal pixel array and draw pixels
    for (i = 0; i < TOP_CUTOFF; ++i) {
        for (j = 0; j < TOPBACKGROUND_WIDTH; ++j) {
            //plot_pixel( j, i, 0xFD18);
            plot_pixel(j, i, TopBackground[i][j]);
        }
    }
}


// Register module functions
module_init(start_video);
module_exit(stop_video);

MODULE_LICENSE("GPL");
