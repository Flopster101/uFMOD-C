#ifndef UFMOD_H
#define UFMOD_H

#include <stddef.h>
#include <stdint.h>
#include "ufmod_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ufmod_context ufmod_t;

enum {
    UFMOD_DEFAULT_RATE = 48000,
    UFMOD_VOL_MAX      = 256
};

enum {
    UFMOD_QUIRK_NONE                      = 0,
    UFMOD_QUIRK_UNCLAMPED_GLOBAL_VOLSLIDE = 1 << 0,
    UFMOD_QUIRK_PERSIST_LOOPING_VOICES    = 1 << 1,
    UFMOD_QUIRK_SKIDROW_LAUNCHER         = UFMOD_QUIRK_UNCLAMPED_GLOBAL_VOLSLIDE |
                                            UFMOD_QUIRK_PERSIST_LOOPING_VOICES
};

ufmod_t* ufmod_load(const void *data, size_t size, unsigned int sample_rate);
size_t   ufmod_render(ufmod_t *ctx, int16_t *dest, size_t num_frames);
size_t   ufmod_render_with_scope(ufmod_t *ctx, int16_t *dest, size_t num_frames,
                                 float *scope, size_t scope_channels);
unsigned int ufmod_get_sample_rate(const ufmod_t *ctx);
unsigned int ufmod_get_channel_count(const ufmod_t *ctx);
void     ufmod_free(ufmod_t *ctx);

void         ufmod_set_volume(ufmod_t *ctx, unsigned int volume);
unsigned int ufmod_get_volume(const ufmod_t *ctx);
unsigned int ufmod_get_time(const ufmod_t *ctx);
void         ufmod_set_noloop(ufmod_t *ctx, int noloop);
int          ufmod_get_loop_count(const ufmod_t *ctx);
void         ufmod_set_target_loops(ufmod_t *ctx, int target_loops);
const char*  ufmod_get_title(const ufmod_t *ctx);
void         ufmod_get_row_order(const ufmod_t *ctx, unsigned int *row, unsigned int *order);
void         ufmod_jump_order(ufmod_t *ctx, int order);
void         ufmod_restart(ufmod_t *ctx);
void         ufmod_get_info(const ufmod_t *ctx, unsigned int *channels, unsigned int *orders, unsigned int *bpm, unsigned int *speed);
int          ufmod_get_channel_volume(const ufmod_t *ctx, unsigned int channel);
void         ufmod_set_quirks(ufmod_t *ctx, unsigned int flags);
unsigned int ufmod_get_quirks(const ufmod_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
