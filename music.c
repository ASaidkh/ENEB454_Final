#define _GNU_SOURCE // Needed for setting CPU affinity
#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <math.h>
#include <sched.h>
#include <stdbool.h>
#include <pthread.h>

// Global Variables
volatile unsigned int *audio_base = NULL;
static bool music_running = false;  // To control game music loop
static pthread_t music_thread;

// Function Prototypes (static for internal use only)
static void *game_music_thread(void *arg);
static void *game_over_thread(void *arg);
static void play_sine_wave(float frequency, int duration_ms);

// Setup Audio 
void setup_audio() {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("Failed to open /dev/mem");
        exit(EXIT_FAILURE);
    }

    // Memory map audio port physical address 
    unsigned int page_offset = AUDIO_BASE & (PAGE_SIZE - 1);
    void *virtual_base = mmap(NULL, AUDIO_SPAN + page_offset, PROT_READ | PROT_WRITE, MAP_SHARED, fd, AUDIO_BASE & ~(PAGE_SIZE - 1));
    if (virtual_base == MAP_FAILED) {
        perror("Failed to mmap");
        close(fd);
        exit(EXIT_FAILURE);
    }

    audio_base = (unsigned int *)((char *)virtual_base + page_offset);
    close(fd);
}

// Clear FIFO
void clear_audio_fifo() {
    *(audio_base + AUDIO_CONTROL) |= 0x10; // Set CW bit
    while (*(audio_base + AUDIO_CONTROL) & 0x10); // Wait for CW bit to clear
}

// Cleanup Audio
void cleanup_audio() {
    if (audio_base) {
        munmap((void *)audio_base, AUDIO_SPAN);
    }
}

// Play a sine wave tone
static void play_sine_wave(float frequency, int duration_ms) {
    int samples = (SAMPLING_RATE * duration_ms) / 1000;
    float phase_increment = 2 * PI * frequency / SAMPLING_RATE;
    float phase = 0.0;

    for (int i = 0; i < samples; i++) {
        while (((*(audio_base + AUDIO_FIFOSPACE) >> 24) & 0xFF) == 0 || // Left FIFO
               ((*(audio_base + AUDIO_FIFOSPACE) >> 16) & 0xFF) == 0);  // Right FIFO

        int sample = (int)(sin(phase) * MAX_VOLUME);
        *(audio_base + AUDIO_LEFTDATA) = sample;
        *(audio_base + AUDIO_RIGHTDATA) = sample;

        phase += phase_increment;
        if (phase >= 2 * PI) {
            phase -= 2 * PI;
        }
    }
}

static void *game_music_thread(void *arg) {
    // Fast-paced melody notes (in Hz) for a game-like feel
    float melody[] = {
        261.63, 329.63, 392.00, 523.25, 440.00, 523.25, 587.33, 659.25, // C, E, G, C, A, C, D, E
        698.46, 784.00, 880.00, 987.77, 1046.50, 1174.66, 1318.51, 1396.91, // F, G, A, B, C, D, E, F
        1527.48, 1760.00, 1864.66, 1975.53, 2093.00, 2207.46, 2349.32, 2489.02  // G, A, B, C, D, E, F, G
    };

    // Durations in ms for a fast-paced tempo (most notes are shorter)
    int durations[] = {
        100, 100, 100, 100, 100, 100, 100, 100, // 8 fast notes (100ms each)
        150, 150, 150, 150, 100, 100, 100, 100, // 4 slightly longer notes (150ms each)
        120, 120, 100, 100, 100, 100, 100, 100  // Mix of 120ms and 100ms notes
    };

    music_running = true;

    while (music_running) {
        // Loop through the melody array and play the notes with their respective durations
        for (int i = 0; i < sizeof(melody) / sizeof(melody[0]) && music_running; i++) {
            play_sine_wave(melody[i], durations[i]);
        }
    }

    return NULL;
}



// Thread function for end game music (plays once, total duration 1 second)
static void *game_over_thread(void *arg) {
    // Melody (1D array with low frequencies for each note)
    float melody[] = {
        110.00, 130.81, 164.81, 174.61, 220.00, 196.00, // A2, C3, E3, F3, A3, G3
        164.81, 174.61, 220.00, 196.00                  // E3, F3, A3, G3
    };

    // Durations for each note (still adds some variation for intensity)
    int durations[] = {
        200, 200, 300, 300, 400, 400, // Slow-paced but impactful
        200, 300, 400, 500            // More emphasis on final notes
    };

    music_running = true;

    // Loop through melody array and play each note with its respective duration
    for (int i = 0; i < sizeof(melody) / sizeof(melody[0]) && music_running; i++) {
        play_sine_wave(melody[i], durations[i]);
    }

    return NULL;
}






// Set CPU affinity for a thread
static void set_thread_cpu(pthread_t thread, int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);

    if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("Failed to set CPU affinity");
        exit(EXIT_FAILURE);
    }
}

// Start game music on a separate thread
void play_game_music() {
    if (music_running) return; // Prevent starting multiple threads

    pthread_create(&music_thread, NULL, game_music_thread, NULL);
    set_thread_cpu(music_thread, 1); // Assign to CPU core 1
}

// Stop game music
void stop_game_music() {
    music_running = false;
    pthread_join(music_thread, NULL); // Wait for thread to terminate
}

// Play game over music on a separate thread
void play_game_over() {
    pthread_t game_over_thread_id;
    pthread_create(&game_over_thread_id, NULL, game_over_thread, NULL);
    set_thread_cpu(game_over_thread_id, 1); // Assign to CPU core 1
    pthread_join(game_over_thread_id, NULL); // Wait for thread to terminate
}
