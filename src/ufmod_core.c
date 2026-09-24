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

#if UFMOD_RUNTIME_QUIRKS
    ctx->quirk_flags = 0;
#if !UFMOD_SETGLOBALVOLUME_ON
    ctx->quirk_flags |= UFMOD_QUIRK_UNCLAMPED_GLOBAL_VOLSLIDE;
#endif
#if UFMOD_PERSIST_LOOPING_VOICES_ON
    ctx->quirk_flags |= UFMOD_QUIRK_PERSIST_LOOPING_VOICES;
#endif
#endif

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

unsigned int ufmod_get_sample_rate(const ufmod_t *ctx) {
    return ctx ? ctx->mix_rate : 0;
}

unsigned int ufmod_get_channel_count(const ufmod_t *ctx) {
    return ctx ? (unsigned int)ctx->numchannels : 0;
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

#if UFMOD_VOL_CONTROL_ON
void ufmod_set_volume(ufmod_t *ctx, unsigned int volume) {
    if (!ctx) return;
    if (volume > UFMOD_VOL_MAX) volume = UFMOD_VOL_MAX;
    ctx->vol_scale = (volume * 32768) / UFMOD_VOL_MAX;
}

unsigned int ufmod_get_volume(const ufmod_t *ctx) {
    if (!ctx) return 0;
    return (ctx->vol_scale * UFMOD_VOL_MAX) / 32768;
}
#endif

#if UFMOD_INFO_API_ON
unsigned int ufmod_get_time(const ufmod_t *ctx) {
    return ctx ? ctx->time_ms : 0;
}
#endif

#if UFMOD_NOLOOP_ON
void ufmod_set_noloop(ufmod_t *ctx, int noloop) {
    if (ctx) ctx->noloop = noloop;
}
#endif

int ufmod_get_loop_count(const ufmod_t *ctx) {
    return ctx ? ctx->loop_count : 0;
}

void ufmod_set_target_loops(ufmod_t *ctx, int target_loops) {
    if (ctx) ctx->target_loops = target_loops;
}

#if UFMOD_INFO_API_ON
const char* ufmod_get_title(const ufmod_t *ctx) {
    return ctx ? ctx->title : "";
}

void ufmod_get_row_order(const ufmod_t *ctx, unsigned int *row, unsigned int *order) {
    if (!ctx) return;
    if (row) *row = (unsigned int)ctx->row;
    if (order) *order = (unsigned int)ctx->order;
}

void ufmod_get_info(const ufmod_t *ctx, unsigned int *channels, unsigned int *orders, unsigned int *bpm, unsigned int *speed) {
    if (!ctx) return;
    if (channels) *channels = (unsigned int)ctx->numchannels;
    if (orders) *orders = (unsigned int)ctx->numorders;
    if (bpm) *bpm = (unsigned int)ctx->defaultbpm;
    if (speed) *speed = (unsigned int)ctx->speed;
}

int ufmod_get_channel_volume(const ufmod_t *ctx, unsigned int channel) {
    if (!ctx || channel >= (unsigned int)ctx->numchannels) return 0;
    const FSOUND_CHANNEL *c0 = &ctx->Channels[channel * 2];
    const FSOUND_CHANNEL *c1 = &ctx->Channels[channel * 2 + 1];
    int v0 = c0->fsptr ? c0->actualvolume : 0;
    int v1 = c1->fsptr ? c1->actualvolume : 0;
    return (v0 > v1) ? v0 : v1;
}
#endif

#if UFMOD_JUMP_TO_PAT_ON
void ufmod_jump_order(ufmod_t *ctx, int order) {
    if (!ctx || ctx->numorders == 0) return;
    if (order < 0) order = 0;
    if (order >= (int)ctx->numorders) order = (int)ctx->numorders - 1;
    ctx->nextorder = order;
    ctx->nextrow = 0;
    ctx->finished = 0;
}

void ufmod_restart(ufmod_t *ctx) {
    if (!ctx) return;
    ctx->order = 0;
    ctx->row = 0;
    ctx->tick = 0;
    ctx->nextorder = -1;
    ctx->nextrow = -1;
    ctx->loop_count = 0;
    ctx->finished = 0;
    ctx->time_ms = 0;
    ctx->globalvolume = 64;
    ctx->patterndelay = 0;
    ctx->mixer_samplesleft = 0;
    memset(ctx->Channels, 0, sizeof(ctx->Channels));
    for (size_t ch = 0; ch < (size_t)ctx->numchannels; ch++) {
        ctx->uFMOD_Ch[ch].cptr = &ctx->Channels[ch * 2];
        ctx->uFMOD_Ch[ch].volume = 0;
        ctx->uFMOD_Ch[ch].envvol = 64;
        ctx->uFMOD_Ch[ch].fadeoutvol = 65536;
        ctx->uFMOD_Ch[ch].keyoff = 0;
    }
}
#endif

void ufmod_set_quirks(ufmod_t *ctx, unsigned int flags) {
#if UFMOD_RUNTIME_QUIRKS
    if (ctx) ctx->quirk_flags = flags;
#else
    (void)ctx;
    (void)flags;
#endif
}

unsigned int ufmod_get_quirks(const ufmod_t *ctx) {
#if UFMOD_RUNTIME_QUIRKS
    return ctx ? ctx->quirk_flags : 0;
#else
    (void)ctx;
    return 0;
#endif
}
