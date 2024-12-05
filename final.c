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
#define TOP_CUTOFF 60
#define VIDEO_BYTES 8
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FPGA_BASE 0xFF200000 // FPGA LW bridge base address
#define KEY_BASE 0xFF200050  // Offset for the KEY input
#define SW_BASE  0xFF200040  // Offset for the SW input

// Ground constants
#define GRASS_WIDTH 56
#define GROUND_HEIGHT 19          // Ground height is now 19 pixels
#define GROUND_Y (SCREEN_HEIGHT - GROUND_HEIGHT) // Ground's top Y position
#define GROUND_COLOR 0x8410 // Gray color

// Player constants
#define PLAYER_X 20            // Player's X position (left side)
#define PLAYER_WIDTH 52        // Player's width from x=20 to x=74
#define PLAYER_HEIGHT 33       // Normal player height (220 - 184 = 36 pixels)
#define PLAYER_CROUCH_HEIGHT 19 // Crouch height (220 - 196 = 24 pixels)
#define PLAYER_COLOR 0x001F    // Blue color
#define PLAYER_JUMP_VELOCITY -6.5f // Initial upward velocity when jumping (negative for upward movement)
#define GRAVITY 0.3f            // Gravity acceleration (positive value, less strong gravity)

// Obstacle constants
#define MAX_OBSTACLES 5
#define OBSTACLE_SPEED 4       // Increased speed for faster map movement
#define OBSTACLE_COLOR 0xF800  // Red color
#define POND_COLOR 0xFC00      // Orange color

// Obstacle dimensions
#define CAT_WIDTH 32
#define CAT_HEIGHT 36
#define CAT_Y (GROUND_Y - CAT_HEIGHT) // y = 200 (200 to 220)

#define MUSHROOM_WIDTH 40
#define MUSHROOM_HEIGHT 40
#define MUSHROOM_Y (GROUND_Y - MUSHROOM_HEIGHT) // y = 180 (180 to 220)

#define CRYSTAL_WIDTH 60
#define CRYSTAL_HEIGHT 200
#define CRYSTAL_Y  0 // y = 0 (0 to 201)

#define POND_WIDTH 67
#define POND_HEIGHT 20                // Height of the lava pit
#define POND_Y GROUND_Y-1               // Position the lava at ground level
#define INVINCIBILITY_DURATION 120 // 3 seconds at 60 FPS (assuming 60 FPS)

// Score display constants
#define SCORE_X 250            // Adjusted X position for score display
#define SCORE_Y 0              // Y position for score display
#define SCORE_COLOR 0xF800     // Red color

// Types of obstacles
typedef enum {
    CAT,
    MUSHROOM,
    CRYSTAL,
    POND
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
    int pond_counter;
    //struct timespec lava_enter_time; // Time when player entered lava
    int is_invincible;
    short int run_frame;
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
    // Draw the top background in the first buffer
    snprintf(command, sizeof(command), "TopBackground");
    write(video_FD, command, strlen(command));

    // Swap buffers
    snprintf(command, sizeof(command), "sync");
    write(video_FD, command, strlen(command));

    // Draw the top background in the second buffer
    snprintf(command, sizeof(command), "TopBackground");
    write(video_FD, command, strlen(command));

    // Swap buffers back to the initial buffer
    snprintf(command, sizeof(command), "sync");
    write(video_FD, command, strlen(command));
    // Animation loop
    while (1) {
        
        
        read_key_inputs();
        update_game_state();

        draw_frame(video_FD);



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
    player.pond_counter = 0;
    player.is_jumping = 0;
    player.is_crouching = 0;
    player.in_lava = 0;
    player.is_invincible = 0;
    player.run_frame = 0;
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


void read_key_inputs() {
    static unsigned int prev_keys = 0x0;
    if (!key_ptr) return;

    unsigned int key_value = *key_ptr & 0xF;
    unsigned int keys = ~key_value & 0xF; // Active low keys

    // Edge detection
    unsigned int key_edge = (keys) & (~prev_keys); // Detect rising edges

    int key1_pressed = keys & 0x2;
    int key2_pressed = keys & 0x4;
    

    int key1_edge = key_edge & 0x2;
    

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
            player.dy += 0.6f; // Adjust this value as needed
        }
    } else {
        if (player.is_crouching) {
            player.is_crouching = 0;
            int delta_height = PLAYER_CROUCH_HEIGHT - PLAYER_HEIGHT;
            player.y += delta_height; // Adjust y to keep bottom position constant
            player.height = PLAYER_HEIGHT;
        }
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
                obstacles[i].type = CAT;
                obstacles[i].width = CAT_WIDTH;
                obstacles[i].height = CAT_HEIGHT;
                obstacles[i].y = CAT_Y;
            } else if (rand_value == 1) {
                obstacles[i].type = MUSHROOM;
                obstacles[i].width = MUSHROOM_WIDTH;
                obstacles[i].height = MUSHROOM_HEIGHT;
                obstacles[i].y = MUSHROOM_Y;
            } else if (rand_value == 2) {
                obstacles[i].type = CRYSTAL;
                obstacles[i].width = CRYSTAL_WIDTH;
                obstacles[i].height = CRYSTAL_HEIGHT;
                obstacles[i].y = CRYSTAL_Y;
            } else {
                obstacles[i].type = POND;
                obstacles[i].width = POND_WIDTH;
                obstacles[i].height = POND_HEIGHT;
                obstacles[i].y = POND_Y; // Over the ground
            }
            break;
        }
    }
}


void check_collisions() {
    int i;
    for (i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {
            if (check_collision(&player, &obstacles[i])) {
                
                if (obstacles[i].type == POND) {
                    // Player is in lava
                    if (!player.in_lava && !player.is_invincible) {
                        game_speed = 100000; 
                        player.in_lava = 1;
                        player.pond_counter = 0;
                    }

                    if (player.in_lava && !player.is_invincible) {
                        int accelVal;
                        accelVal = read_accel(accel_FD);
                        if ( accelVal >= 500){
                            // Player escaped lava
                            player.in_lava = 0;
                            player.pond_counter = 0;
                            game_speed = 20000; // Restore game speed
                            // Start invincibility
                            player.is_invincible = 1;
                            player.invincibility_start_frame = frame_count;
                           
                        }
                        // Check if player has been stuck in pond for 2 seconds
                        else if (player.pond_counter > 2 /(game_speed * 0.000001) ) {
                             game_over = 1;
                        }
                        else {
                            player.pond_counter++;
                        }
                        if (accelVal != -1) {
                            printf("Accel Value = %d\n", accelVal);
                        }

                    }
                
                } else {
                    // Collision with other obstacles results in immediate game over
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
        if (obstacles[i].active && obstacles[i].type == POND) {
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

    // Draw obstacles
    draw_obstacles(video_FD);

    // Draw player
    draw_player(video_FD);

    // Display score
    display_score(video_FD);

    // If game over, display game over message
    if (game_over) {
        display_game_over(video_FD);
    }

    char command[64];
    // Synchronize with VGA
    snprintf(command, sizeof(command), "sync");
    write(video_FD, command, strlen(command));
}

// Function to draw the ground and moving texture lines
void draw_ground(int video_FD) {
    char command[64];
    

    int grass_x;
    // Draw grass repeated grass obstacles for ground
    for (grass_x = 0; grass_x < SCREEN_WIDTH; grass_x += GRASS_WIDTH) {
        snprintf(command, sizeof(command), "Grass %d,%d", grass_x, GROUND_Y);
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
    if (player.is_crouching){
        snprintf(command, sizeof(command), "DogCrouch %d,%d", player.x, player_y_int);
    }
    else if (player.is_jumping){
        snprintf(command, sizeof(command), "DogRun3 %d,%d", player.x, player_y_int);
    }
    else if (player.run_frame < 5) {
        snprintf(command, sizeof(command), "DogRun1 %d,%d", player.x, player_y_int);
        player.run_frame++;
    }
    else if (player.run_frame < 10) {
        snprintf(command, sizeof(command), "DogRun2 %d,%d", player.x, player_y_int);
        player.run_frame++;
    }
    else if (player.run_frame < 15) {
        snprintf(command, sizeof(command), "DogRun3 %d,%d", player.x, player_y_int);
        player.run_frame++;
    }
    else {
        snprintf(command, sizeof(command), "DogRun3 %d,%d", player.x, player_y_int);
        player.run_frame = 0;
    }

    write(video_FD, command, strlen(command));
}


// Function to draw the obstacles
void draw_obstacles(int video_FD) {
    char command[64];
    int i;
    for (i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {

            // Draw obstacles
            switch(obstacles[i].type) {
                // Draw cat obstacle
                case CAT: 
                     snprintf(command, sizeof(command), "Cat %d,%d",
                     obstacles[i].x, obstacles[i].y);
                     write(video_FD, command, strlen(command));
                     break;
                // Draw mushroom obstacle
                case MUSHROOM: 
                     snprintf(command, sizeof(command), "Mushroom %d,%d",
                     obstacles[i].x, obstacles[i].y);
                     write(video_FD, command, strlen(command));
                     break;
                // Draw crystal obstacle
                case CRYSTAL: 
                     snprintf(command, sizeof(command), "Crystal %d,%d", obstacles[i].x, obstacles[i].y);
                     write(video_FD, command, strlen(command));
                     break;
                // Draw pond obstacle
                case POND: 
                     snprintf(command, sizeof(command), "Pond %d,%d",
                     obstacles[i].x, obstacles[i].y);
                     write(video_FD, command, strlen(command));
                     break;
                default:
                    printf("Error Drawing obstacle\n");
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
