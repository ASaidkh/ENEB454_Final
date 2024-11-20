// Corrected part6.c with issues fixed
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "accelRead.h" 

// Constants
#define VIDEO_BYTES 8
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FPGA_BASE 0xFF200000 // FPGA LW bridge base address
#define KEY_BASE 0xFF200050  // Offset for the KEY input
#define SW_BASE  0xFF200040  // Offset for the SW input

// Ground constants
#define GROUND_HEIGHT 19          // Ground height is now 19 pixels
#define GROUND_Y (SCREEN_HEIGHT - GROUND_HEIGHT) // Ground's top Y position
#define GROUND_COLOR 0x8410 // Gray color

// Player constants
#define PLAYER_X 20            // Player's X position (left side)
#define PLAYER_WIDTH 54        // Player's width from x=20 to x=74
#define PLAYER_HEIGHT 36       // Normal player height (220 - 184 = 36 pixels)
#define PLAYER_CROUCH_HEIGHT 19 // Crouch height (220 - 196 = 24 pixels)
#define PLAYER_COLOR 0x001F    // Blue color
#define PLAYER_JUMP_VELOCITY -6.0f // Initial upward velocity when jumping (negative for upward movement)
#define GRAVITY 0.2f            // Gravity acceleration (positive value, less strong gravity)

// Obstacle constants
#define MAX_OBSTACLES 5
#define OBSTACLE_SPEED 4       // Increased speed for faster map movement
#define OBSTACLE_COLOR 0xF800  // Red color
#define LAVA_COLOR 0xFC00      // Orange color

// Obstacle dimensions
#define SMALL_PILLAR_WIDTH 20
#define SMALL_PILLAR_HEIGHT 20
#define SMALL_PILLAR_Y (GROUND_Y - SMALL_PILLAR_HEIGHT) // y = 200 (200 to 220)

#define MUSHROOM_WIDTH 40
#define MUSHROOM_HEIGHT 40
#define MUSHROOM_Y (GROUND_Y - MUSHROOM_HEIGHT) // y = 180 (180 to 220)

#define TALL_PILLAR_WIDTH 65
#define TALL_PILLAR_HEIGHT 190
#define TALL_PILLAR_Y 0 // y = 0 (0 to 201)

#define LAVA_WIDTH 67
#define LAVA_HEIGHT 25                // Height of the lava pit
#define LAVA_Y GROUND_Y-1               // Position the lava at ground level
#define INVINCIBILITY_DURATION 120 // 3 seconds at 60 FPS (assuming 60 FPS)

// Score display constants
#define SCORE_X 250            // Adjusted X position for score display
#define SCORE_Y 0              // Y position for score display
#define SCORE_COLOR 0xF800     // Red color

// Types of obstacles
typedef enum {
    PILLAR_SMALL,
    MUSHROOM,
    PILLAR_TALL,
    LAVA
} ObstacleType;

// Player structure
// Player structure
typedef struct {
    int x;          // x position
    float y;        // y position (float for smoother movement)
    int width, height; // Dimensions
    float dy;          // Vertical velocity (float)
    int is_jumping;
    int is_crouching;
    int in_lava;
    //struct timespec lava_enter_time; // Time when player entered lava
    int is_invincible;
    unsigned int invincibility_start_frame;
} Player;



// Obstacle structure
typedef struct {
    int x, y;          // Position (top-left corner)
    int width, height; // Dimensions
    ObstacleType type; // Type of obstacle
    int active;
} Obstacle;

// Global variables
volatile unsigned int *key_ptr = NULL;
volatile unsigned int *sw_ptr = NULL;

Player player;
Obstacle obstacles[MAX_OBSTACLES];
unsigned int frame_count = 0;
int game_over = 0;
int game_speed = 20000; // Decreased sleep duration for faster gameplay (microseconds)
unsigned int last_obstacle_time = 0;
unsigned int obstacle_interval = 100; // Generate obstacle every 100 frames
// Dynamic game speed variables
unsigned int next_speed_increase = 1000; // Next score threshold for speed boost
const int speed_decrement = 500;          // Amount to decrease game_speed each boost
const unsigned int speed_interval = 1000; // Score interval for speed increase
const unsigned int min_game_speed = 5000; // Minimum allowable game_speed (in microseconds)

// Function prototypes
int setup_mmap();
void initialize_game();
void read_key_inputs();
void update_game_state();
void draw_frame(int video_FD);
void draw_ground(int video_FD);
void draw_player(int video_FD);
void draw_obstacles(int video_FD);
void generate_obstacle();
void move_obstacles();
void check_collisions();
int check_collision(Player *player, Obstacle *obstacle);
int check_collision_with_lava();
void display_score(int video_FD);
void display_game_over(int video_FD);
void cleanup(int video_FD);

// Aceloremeter driver file ID
int accel_FD;

int main() {
    int video_FD;
    accel_FD = open_accel(); // Acceloremeter driver file ID
    char command[64];

    if (setup_mmap() == -1) {
        return -1; // Fail if memory mapping didn't work
    }

    // Open the video device driver
    if ((video_FD = open("/dev/video", O_RDWR)) == -1) {
        printf("Error opening /dev/video: %s\n", strerror(errno));
        return -1;
    }

    // Initialize game
    initialize_game();

    // Animation loop
    while (1) {
        // Synchronize with VGA
        
        read_key_inputs();
        update_game_state();

        draw_frame(video_FD);
        snprintf(command, sizeof(command), "sync");
        write(video_FD, command, strlen(command));

        usleep(game_speed); // Control speed

        frame_count++;

        if (game_over) {
            // Game over, display message and exit after a delay
            sleep(1);
            break;
        }
    }

    cleanup(video_FD);
    return 0;
}

// Function to set up memory mapping for the switches and keys
int setup_mmap() {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) {
        perror("Error opening /dev/mem");
        return -1;
    }

    // Map the Lightweight bridge
    void *lw_virtual = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, FPGA_BASE);
    if (lw_virtual == MAP_FAILED) {
        perror("Error mapping FPGA LW bridge");
        close(fd);
        return -1;
    }

    // Set up pointers to the key and switch registers
    key_ptr = (volatile unsigned int *)(lw_virtual + (KEY_BASE - FPGA_BASE));
    sw_ptr = (volatile unsigned int *)(lw_virtual + (SW_BASE - FPGA_BASE));

    close(fd);
    return 0;
}

// Function to initialize the game state
void initialize_game() {
    // Initialize player
    player.x = PLAYER_X;
    player.width = PLAYER_WIDTH;
    player.height = PLAYER_HEIGHT;
    player.y = GROUND_Y - player.height; // Player's top Y position when standing
    player.dy = 0.0f;
    player.is_jumping = 0;
    player.is_crouching = 0;
    player.in_lava = 0;
    
    // Initialize lava_enter_time to zero
  //  player.lava_enter_time.tv_sec = 0;
  //  player.lava_enter_time.tv_nsec = 0;
    
    player.is_invincible = 0;
    player.invincibility_start_frame = 0;

    // Initialize obstacles
    int i;
    for (i = 0; i < MAX_OBSTACLES; i++) {
        obstacles[i].active = 0; // No active obstacles at the start
    }

    frame_count = 0;
    game_over = 0;
    srand(time(NULL)); // Seed random number generator
}






// void read_key_inputs() {
//     static unsigned int prev_keys = 0x0;
//     if (!key_ptr) return;

//     unsigned int key_value = *key_ptr & 0xF;
//     unsigned int keys = ~key_value & 0xF; // Active low keys

//     // Edge detection
//     unsigned int key_edge = (keys) & (~prev_keys); // Detect rising edges

//     int key1_pressed = keys & 0x2;
//     int key2_pressed = keys & 0x4;
//     int key3_pressed = keys & 0x8;

//     int key1_edge = key_edge & 0x2;
//     int key3_edge = key_edge & 0x8;

//     // Jump when KEY1 is pressed (edge detection)
//     if (key1_edge && !player.is_jumping && !player.in_lava) {
//         player.dy = PLAYER_JUMP_VELOCITY;
//         player.is_jumping = 1;
//     }

//     // Crouch while KEY2 is held down
//     if (!key2_pressed) {
//         if (!player.is_jumping) {
//             player.is_crouching = 1;
//             player.height = PLAYER_CROUCH_HEIGHT;
//             player.y = GROUND_Y - player.height; // Adjust y position
//         } else{
//             player.is_crouching = 1;
//             player.height = PLAYER_CROUCH_HEIGHT;
//             player.y = GROUND_Y - player.height; // Adjust y position
//             player.dy -=.1;
//         }
//     } else {
//         if (player.is_crouching) {
//             player.is_crouching = 0;
//             player.height = PLAYER_HEIGHT;
//             player.y = GROUND_Y - player.height; // Adjust y position
//         }
//     }

//     // Escape lava when KEY3 is pressed
//     if (player.in_lava && key3_edge) {
//         // Player escaped lava
//         player.in_lava = 0;
//         game_speed = 20000; // Restore game speed
//         // Start invincibility
//         player.is_invincible = 1;
//         player.invincibility_start_frame = frame_count;
//     }

//     prev_keys = keys;
// }


void read_key_inputs() {
    static unsigned int prev_keys = 0x0;
    if (!key_ptr) return;

    unsigned int key_value = *key_ptr & 0xF;
    unsigned int keys = ~key_value & 0xF; // Active low keys

    // Edge detection
    unsigned int key_edge = (keys) & (~prev_keys); // Detect rising edges

    int key1_pressed = keys & 0x2;
    int key2_pressed = keys & 0x4;
    int key3_pressed = keys & 0x8;

    int key1_edge = key_edge & 0x2;
    int key3_edge = key_edge & 0x8;

    // Jump when KEY1 is pressed (edge detection)
    if (key1_edge && !player.is_jumping && !player.in_lava) {
        player.dy = PLAYER_JUMP_VELOCITY;
        player.is_jumping = 1;
    }

    // Crouch while KEY2 is held down
    if (!key2_pressed) { // Corrected logic
        if (!player.is_crouching) {
            player.is_crouching = 1;
            int delta_height = PLAYER_HEIGHT - PLAYER_CROUCH_HEIGHT;
            player.y += delta_height; // Adjust y to keep bottom position constant
            player.height = PLAYER_CROUCH_HEIGHT;
        }
        if (player.is_jumping) {
            // Increase downward velocity to fall faster
            player.dy += 0.5f; // Adjust this value as needed
        }
    } else {
        if (player.is_crouching) {
            player.is_crouching = 0;
            int delta_height = PLAYER_CROUCH_HEIGHT - PLAYER_HEIGHT;
            player.y += delta_height; // Adjust y to keep bottom position constant
            player.height = PLAYER_HEIGHT;
        }
    }

    // Escape lava when KEY3 is pressed
    if (player.in_lava && key3_edge) {
        // Player escaped lava
        player.in_lava = 0;
        game_speed = 20000; // Restore game speed
        // Start invincibility
        player.is_invincible = 1;
        player.invincibility_start_frame = frame_count;
    }

    prev_keys = keys;
}




// Function to update the game state
void update_game_state() {
    if (game_over) return;

    // Update player position
    if (player.is_jumping) {
        player.dy += GRAVITY; // Apply gravity
        player.y += player.dy;
        // Check if player lands on the ground
        if (player.y >= GROUND_Y - player.height - 0.1f) { // Use a small epsilon
            player.y = GROUND_Y - player.height;
            player.dy = 0.0f;
            player.is_jumping = 0;
        }
    }

    // Update obstacles
    move_obstacles();

    // Generate new obstacles periodically
    if (frame_count - last_obstacle_time >= obstacle_interval) {
        generate_obstacle();
        last_obstacle_time = frame_count;
    }

    // Check collisions
    check_collisions();

    // Check if it's time to increase game speed and gravity
    if (frame_count >= next_speed_increase) {
        // Decrease game_speed to make the game run faster, ensuring it doesn't go below the minimum
        if (game_speed > min_game_speed) {
            game_speed -= speed_decrement;
            if (game_speed < min_game_speed) {
                game_speed = min_game_speed; // Enforce minimum game_speed
            }
            printf("Game speed decreased to %u microseconds at frame %u\n", game_speed, frame_count);
        }

    
        // Set the next score threshold for the subsequent speed and gravity boost
        next_speed_increase += speed_interval;
    }

}

// Function to move obstacles
void move_obstacles() {
    int i;
    for (i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {
            obstacles[i].x -= OBSTACLE_SPEED;
            // Deactivate obstacle if it moves off screen
            if (obstacles[i].x + obstacles[i].width < 0) {
                obstacles[i].active = 0;
            }
        }
    }
}

// Function to generate obstacles
void generate_obstacle() {
    // Find an inactive obstacle slot
    int i;
    for (i = 0; i < MAX_OBSTACLES; i++) {
        if (!obstacles[i].active) {
            // Initialize obstacle
            obstacles[i].x = SCREEN_WIDTH;
            obstacles[i].active = 1;

            // Randomly select obstacle type
            int rand_value = rand() % 4;
            if (rand_value == 0) {
                obstacles[i].type = PILLAR_SMALL;
                obstacles[i].width = SMALL_PILLAR_WIDTH;
                obstacles[i].height = SMALL_PILLAR_HEIGHT;
                obstacles[i].y = SMALL_PILLAR_Y;
            } else if (rand_value == 1) {
                obstacles[i].type = MUSHROOM;
                obstacles[i].width = MUSHROOM_WIDTH;
                obstacles[i].height = MUSHROOM_HEIGHT;
                obstacles[i].y = MUSHROOM_Y;
            } else if (rand_value == 2) {
                obstacles[i].type = PILLAR_TALL;
                obstacles[i].width = TALL_PILLAR_WIDTH;
                obstacles[i].height = TALL_PILLAR_HEIGHT;
                obstacles[i].y = TALL_PILLAR_Y;
            } else {
                obstacles[i].type = LAVA;
                obstacles[i].width = LAVA_WIDTH;
                obstacles[i].height = LAVA_HEIGHT;
                obstacles[i].y = LAVA_Y; // Over the ground
            }
            break;
        }
    }
}




// void check_collisions() {
//     int i;
//     for (i = 0; i < MAX_OBSTACLES; i++) {
//         if (obstacles[i].active) {
//             if (check_collision(&player, &obstacles[i])) {
//                 if (obstacles[i].type == LAVA) {
//                     // Player is in lava
//                     if (!player.in_lava && !player.is_invincible) {
//                         // Player just entered lava
//                         game_speed = 100000; // Slow down the game
//                         player.in_lava = 1;
//                         player.lava_enter_frame = frame_count;
//                     }
//                     // Check if 3 seconds have passed
//                     if (player.in_lava && !player.is_invincible) {
//                         if (frame_count - player.lava_enter_frame >= 180) { // Assuming 60 FPS
//                             // 3 seconds have passed, game over
//                             game_over = 1;
//                         }
//                     }
//                     // Player can press KEY3 to escape (handled in read_key_inputs)
//                 } else {
//                     // Collision with pillar results in immediate game over
//                     if (!player.is_invincible) {
//                         game_over = 1;
//                     }
//                 }
//             }
//         }
//     }
//     // If player is in lava but not colliding with lava anymore
//     if (player.in_lava && !check_collision_with_lava()) {
//         // Player escaped lava
//         game_speed = 20000; // Restore game speed
//         player.in_lava = 0;
//         // Start invincibility
//         player.is_invincible = 1;
//         player.invincibility_start_frame = frame_count;
//     }

//     // Handle invincibility duration
//     if (player.is_invincible) {
//         if (frame_count - player.invincibility_start_frame >= INVINCIBILITY_DURATION) {
//             // Invincibility period over
//             player.is_invincible = 0;
//         }
//     }
// }

void check_collisions() {
    int i;
    for (i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {
            if (check_collision(&player, &obstacles[i])) {
                
                if (obstacles[i].type == LAVA) {
                    // Player is in lava
                    if (!player.in_lava && !player.is_invincible) {
                        // Player just entered lava
                        game_speed = 100000; // Slow down the game
                        player.in_lava = 1;
                        //clock_gettime(CLOCK_MONOTONIC, &player.lava_enter_time);
                    }
                    if (player.in_lava && !player.is_invincible) {
                        // Check if 3 seconds have passed
                        /*struct timespec current_time;
                        clock_gettime(CLOCK_MONOTONIC, &current_time);
                        double elapsed_time = (current_time.tv_sec - player.lava_enter_time.tv_sec)
                            + (current_time.tv_nsec - player.lava_enter_time.tv_nsec) / 1e9;
                        if (elapsed_time >= 2.0) {
                            // 3 seconds have passed, game over
                            game_over = 1;
                        }
                        */
                       printf("Acceloremeter Value = %d\n", read_accel(accel_FD));
                    }
                    // Player can press KEY3 to escape (handled in read_key_inputs)
                } else {
                    // Collision with pillar results in immediate game over
                    if (!player.is_invincible) {
                        game_over = 1;
                    }
                 }
            }
        }
    }
    // If player is in lava but not colliding with lava anymore
    if (player.in_lava && !check_collision_with_lava()) {
        // Player escaped lava
        game_speed = 20000; // Restore game speed
        player.in_lava = 0;
        // Start invincibility
        player.is_invincible = 1;
        player.invincibility_start_frame = frame_count;
    }

    // Handle invincibility duration
    if (player.is_invincible) {
        if (frame_count - player.invincibility_start_frame >= INVINCIBILITY_DURATION) {
            // Invincibility period over
            player.is_invincible = 0;
        }
    }
}





// Function to check collision between player and an obstacle
int check_collision(Player *player, Obstacle *obstacle) {
    int player_left = player->x;
    int player_right = player->x + player->width - 1;
    int player_top = (int)(player->y + 0.5f);
    int player_bottom = player_top + player->height - 1;

    int obstacle_left = obstacle->x;
    int obstacle_right = obstacle->x + obstacle->width - 1;
    int obstacle_top = obstacle->y;
    int obstacle_bottom = obstacle->y + obstacle->height - 1;

    if (player_right >= obstacle_left && player_left <= obstacle_right &&
        player_bottom >= obstacle_top && player_top <= obstacle_bottom) {
        return 1; // Collision detected
    }
    return 0; // No collision
}

// Function to check if player is still colliding with lava
int check_collision_with_lava() {
    int i;
    for (i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active && obstacles[i].type == LAVA) {
            if (check_collision(&player, &obstacles[i])) {
                return 1;
            }
        }
    }
    return 0;
}

// Function to draw the entire frame
void draw_frame(int video_FD) {
    // Clear the screen
    write(video_FD, "clear", 5);

    // Draw ground
    draw_ground(video_FD);

    // Draw player
    draw_player(video_FD);

    // Draw obstacles
    draw_obstacles(video_FD);

    // Display score
    display_score(video_FD);

    // If game over, display game over message
    if (game_over) {
        display_game_over(video_FD);
    }
}

// Function to draw the ground and moving texture lines
void draw_ground(int video_FD) {
    char command[64];
    // Draw ground rectangle
    snprintf(command, sizeof(command), "box 0,%d %d,%d 0x%04X", GROUND_Y, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, GROUND_COLOR);
    write(video_FD, command, strlen(command));

    // Draw texture lines
    int i;
    int line_x;
    int texture_speed = OBSTACLE_SPEED;
    static int texture_offset = 0;
    texture_offset = (texture_offset + texture_speed) % 50; // 50 is the spacing between lines
    for (i = 0; i < 6; i++) {
        line_x = SCREEN_WIDTH - (i * 50 + texture_offset);
        if (line_x < 0) line_x += SCREEN_WIDTH;
        snprintf(command, sizeof(command), "line %d,%d %d,%d 0x0000", line_x, GROUND_Y, line_x, SCREEN_HEIGHT - 1);
        write(video_FD, command, strlen(command));
    }
}

// Function to draw the player
void draw_player(int video_FD) {
    int player_y_int = (int)(player.y + 0.5f); // Round to nearest integer

    // If player is invincible, make them flash
    if (player.is_invincible) {
        if ((frame_count / 10) % 2 == 0) {
            // Skip drawing the player every other 10 frames
            return;
        }
    }
    char command[64];
    snprintf(command, sizeof(command), "DogRun1 %d,%d", player.x, player_y_int);
    write(video_FD, command, strlen(command));
}


// Function to draw the obstacles
void draw_obstacles(int video_FD) {
    char command[64];
    int i;
    for (i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {

            if (obstacles[i].type == MUSHROOM ) {
                snprintf(command, sizeof(command), "Mushroom %d,%d",
                     obstacles[i].x, obstacles[i].y);
                     write(video_FD, command, strlen(command));
            }
            else {
            int x1 = obstacles[i].x;
            int y1 = obstacles[i].y;
            int x2 = obstacles[i].x + obstacles[i].width - 1;
            int y2 = obstacles[i].y + obstacles[i].height - 1;

            // Check if the obstacle is within the screen
            if (x2 < 0 || x1 >= SCREEN_WIDTH || y2 < 0 || y1 >= SCREEN_HEIGHT) {
                continue; // Skip drawing if completely off-screen
            }

            // Clip coordinates to screen boundaries
            if (x1 < 0) x1 = 0;
            if (x2 >= SCREEN_WIDTH) x2 = SCREEN_WIDTH - 1;
            if (y1 < 0) y1 = 0;
            if (y2 >= SCREEN_HEIGHT) y2 = SCREEN_HEIGHT - 1;

            unsigned short color = (obstacles[i].type == LAVA) ? LAVA_COLOR : OBSTACLE_COLOR;
            snprintf(command, sizeof(command), "box %d,%d %d,%d 0x%04X",
                     x1, y1, x2, y2, color);
            write(video_FD, command, strlen(command));
            }
        }
    }
}


// Function to display the score
void display_score(int video_FD) {
    char command[128];
    char text[64];
    snprintf(text, sizeof(text), "SScore: %u", frame_count);

    // Erase previous text by writing spaces over it
    snprintf(command, sizeof(command), "text %d,%d                     ", SCORE_X, SCORE_Y);
    write(video_FD, command, strlen(command));

    // Write new score
    snprintf(command, sizeof(command), "text %d,%d %s", SCORE_X, SCORE_Y, text);
    write(video_FD, command, strlen(command));
}

// Function to display "Game Over" message
void display_game_over(int video_FD) {
    char command[128];
    static int flash = 0;
    flash = !flash;

    if (flash) {
        // Display "Game Over" message
        snprintf(command, sizeof(command), "text 100,100 Game Over");
        write(video_FD, command, strlen(command));
    } else {
        // Erase "Game Over" message
        snprintf(command, sizeof(command), "text 100,100           ");
        write(video_FD, command, strlen(command));
    }
}

// Function to clean up resources
void cleanup(int video_FD) {
    // Close the video device
    close(video_FD);
}
