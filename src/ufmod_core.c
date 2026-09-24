#include <stdlib.h>
#include <string.h>
#include "ufmod.h"
#include "ufmod_types.h"

int    ufmod_load_xm(ufmod_t *ctx, const void *data, size_t size);
size_t ufmod_render_frames(ufmod_t *ctx, int16_t *dest, size_t num_frames);

ufmod_t* ufmod_load(const void *data, size_t size, unsigned int sample_rate) {
    if (!data || size < 60) return NULL;

    ufmod_t *ctx = (ufmod_t*)calloc(1, sizeof(ufmod_t));
    if (!ctx) return NULL;

    ctx->mix_rate = sample_rate ? sample_rate : UFMOD_DEFAULT_RATE;
    ctx->vol_scale = 32768;
    ctx->nextorder = -1;
    ctx->nextrow = -1;

    if (!ufmod_load_xm(ctx, data, size)) {
        ufmod_free(ctx);
        return NULL;
    }

    return ctx;
}

size_t ufmod_render(ufmod_t *ctx, int16_t *dest, size_t num_frames) {
    if (!ctx || !dest || num_frames == 0) return 0;
    return ufmod_render_frames(ctx, dest, num_frames);
}

void ufmod_free(ufmod_t *ctx) {
    if (!ctx) return;

    if (ctx->pattern) {
        for (size_t i = 0; i < ctx->numpatternsmem; i++) {
            if (ctx->pattern[i].data) {
                free(ctx->pattern[i].data);
            }
        }
        free(ctx->pattern);
    }

    if (ctx->instrument) {
        for (size_t i = 0; i < ctx->numinsts; i++) {
            for (size_t s = 0; s < 16; s++) {
                FSOUND_SAMPLE *sptr = ctx->instrument[i].sample[s];
                if (sptr) {
                    if (sptr->buff) free(sptr->buff);
                    free(sptr);
                }
            }
        }
        free(ctx->instrument);
    }

    free(ctx);
}

void ufmod_set_volume(ufmod_t *ctx, unsigned int volume) {
    if (!ctx) return;
    if (volume > UFMOD_VOL_MAX) volume = UFMOD_VOL_MAX;
    ctx->vol_scale = (volume * 32768) / UFMOD_VOL_MAX;
}

unsigned int ufmod_get_volume(const ufmod_t *ctx) {
    if (!ctx) return 0;
    return (ctx->vol_scale * UFMOD_VOL_MAX) / 32768;
}

unsigned int ufmod_get_time(const ufmod_t *ctx) {
    return ctx ? ctx->time_ms : 0;
}

void ufmod_set_noloop(ufmod_t *ctx, int noloop) {
    if (ctx) ctx->noloop = noloop;
}

int ufmod_get_loop_count(const ufmod_t *ctx) {
    return ctx ? ctx->loop_count : 0;
}

void ufmod_set_target_loops(ufmod_t *ctx, int target_loops) {
    if (ctx) ctx->target_loops = target_loops;
}

const char* ufmod_get_title(const ufmod_t *ctx) {
    return ctx ? ctx->title : "";
}

void ufmod_get_row_order(const ufmod_t *ctx, unsigned int *row, unsigned int *order) {
    if (!ctx) return;
    if (row) *row = (unsigned int)ctx->row;
    if (order) *order = (unsigned int)ctx->order;
}
