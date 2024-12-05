#ifndef MUSIC_H
#define MUSIC_H


// Macros
#define AUDIO_BASE 0xFF203040
#define AUDIO_SPAN 0x40
#define AUDIO_CONTROL 0
#define AUDIO_FIFOSPACE 1
#define AUDIO_LEFTDATA 2
#define AUDIO_RIGHTDATA 3
#define PAGE_SIZE 4096
#define MAX_VOLUME 0x7FFFFFFF
#define SAMPLING_RATE 8000
#define PI 3.14159265358979323846

// Function Prototypes
void setup_audio();
void clear_audio_fifo();
void cleanup_audio();
void play_game_music();
void stop_game_music();
void play_game_over();

#endif // MUSIC_H
