#ifndef FEATHERTALK_PLAYER_H
#define FEATHERTALK_PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FT_PLAYER_MAX_TRACKS 24U
#define FT_PLAYER_PATH_MAX   256U
#define FT_PLAYER_NAME_MAX   96U

typedef enum
{
    FT_PLAYER_CODEC_PCM_WAV = 0,
    FT_PLAYER_CODEC_MP3
} ft_player_codec_t;

typedef enum
{
    FT_PLAYER_STOPPED = 0,
    FT_PLAYER_STARTING,
    FT_PLAYER_PLAYING,
    FT_PLAYER_PAUSED,
    FT_PLAYER_ERROR
} ft_player_state_t;

typedef struct
{
    char path[FT_PLAYER_PATH_MAX];
    char name[FT_PLAYER_NAME_MAX];
    uint64_t file_bytes;
    uint32_t data_bytes;
    uint32_t duration_ms;
    uint32_t sample_rate;
    uint8_t sample_bits;
    uint8_t channels;
    ft_player_codec_t codec;
    bool recording;
} ft_player_track_t;

typedef struct
{
    ft_player_state_t state;
    size_t track_count;
    size_t current_track;
    uint32_t position_ms;
    uint32_t duration_ms;
    uint32_t sample_rate;
    uint8_t sample_bits;
    uint8_t channels;
    bool folder_loop;
    uint32_t generation;
    int last_error;
} ft_player_status_t;

/* The media library is the direct children of one user-selected directory.
 * PCM WAV and MP3 are supported; subdirectories are intentionally not scanned
 * recursively so that a folder is also an explicit playlist boundary. */
int ft_player_set_directory(const char *path);
int ft_player_get_directory(char *path, size_t path_size);
int ft_player_scan(void);
size_t ft_player_get_track_count(void);
int ft_player_get_track(size_t index, ft_player_track_t *track);
int ft_player_set_folder_loop(bool enabled);
int ft_player_play(size_t index);
int ft_player_pause(void);
int ft_player_resume(void);
int ft_player_stop(void);
int ft_player_get_status(ft_player_status_t *status);

#endif /* FEATHERTALK_PLAYER_H */
