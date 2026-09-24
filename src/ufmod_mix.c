#include <stdlib.h>
#include <string.h>
#include "ufmod_types.h"

void ufmod_do_note(ufmod_t *ctx);
void ufmod_do_effs(ufmod_t *ctx);

static void mix_channel(ufmod_t *ctx, size_t channel, FSOUND_CHANNEL *sc, int32_t *mix_buf, size_t num_samples) {
    FSOUND_SAMPLE *sptr = sc->fsptr;
    if (!sptr || !sptr->buff || sptr->length == 0) return;

    int32_t target_L = sc->leftvolume << FSOUND_RAMP_POW;
    int32_t target_R = sc->rightvolume << FSOUND_RAMP_POW;

    if (target_L == 0 && target_R == 0 && sc->ramp_leftvolume == 0 && sc->ramp_rightvolume == 0 && sc->ramp_count == 0) {
        return;
    }

    if (sc->ramp_count == 0) {
        sc->ramp_leftspeed = (target_L - sc->ramp_leftvolume) >> FSOUND_RAMP_POW;
        sc->ramp_rightspeed = (target_R - sc->ramp_rightvolume) >> FSOUND_RAMP_POW;
        if (sc->ramp_leftspeed || sc->ramp_rightspeed) {
            sc->ramp_count = FSOUND_RAMP_STEPS;
        }
    }

    uint32_t loopstart = sptr->loopstart;
    uint32_t looplen = sptr->looplen;
    uint32_t loopend = loopstart + looplen;
    uint8_t loopmode = sptr->loopmode;

    for (size_t i = 0; i < num_samples; i++) {
        uint32_t pos = sc->mixpos;
        if (loopmode == 0 && pos >= sptr->length) {
            sc->fsptr = NULL;
            break;
        }

        int16_t s0 = sptr->buff[pos];
        int16_t s1 = sptr->buff[pos + 1];
        int32_t diff = (int32_t)s1 - (int32_t)s0;
        int32_t interp = (int32_t)(((int64_t)diff * (sc->mixposlo >> 1)) >> 31);
        int32_t sample = (int32_t)s0 + interp;

        if (ctx->scope_buffer && ctx->scope_channels > 0 && (size_t)(channel >> 1) < ctx->scope_channels) {
            size_t scope_index = (size_t)(channel >> 1) * ctx->scope_samples + ctx->scope_offset + i;
            ctx->scope_buffer[scope_index] += (float)sample / 32768.0f;
        }

        int32_t l_sample = (int32_t)(((int64_t)sample * sc->ramp_leftvolume) >> 1);
        int32_t r_sample = (int32_t)(((int64_t)sample * sc->ramp_rightvolume) >> 1);

        mix_buf[i * 2 + 0] += l_sample;
        mix_buf[i * 2 + 1] += r_sample;

        if (sc->ramp_count > 0) {
            sc->ramp_leftvolume += sc->ramp_leftspeed;
            sc->ramp_rightvolume += sc->ramp_rightspeed;
            sc->ramp_count--;
            if (sc->ramp_count == 0) {
                sc->ramp_leftvolume = target_L;
                sc->ramp_rightvolume = target_R;
#if UFMOD_RUNTIME_QUIRKS
                if (target_L == 0 && target_R == 0) {
                    if (!(ctx->quirk_flags & UFMOD_QUIRK_PERSIST_LOOPING_VOICES) && sc->actualvolume == 0) {
                        sc->fsptr = NULL;
                    }
                    break;
                }
#else
#if !UFMOD_PERSIST_LOOPING_VOICES_ON
                if (target_L == 0 && target_R == 0 && sc->actualvolume == 0) {
                    sc->fsptr = NULL;
                    break;
                }
#else
                if (target_L == 0 && target_R == 0) {
                    break;
                }
#endif
#endif
            }
        }

        uint64_t full_pos = ((uint64_t)sc->mixpos << 32) | sc->mixposlo;
        uint64_t step = ((uint64_t)sc->speedhi << 32) | sc->speedlo;

        if (sc->speeddir == 0) {
            full_pos += step;
            sc->mixpos = (uint32_t)(full_pos >> 32);
            sc->mixposlo = (uint32_t)(full_pos & 0xFFFFFFFF);

            if (loopmode == 1) { // Normal forward loop
                if (sc->mixpos >= loopend) {
                    if (looplen > 0) {
                        sc->mixpos = loopstart + ((sc->mixpos - loopend) % looplen);
                    } else {
                        sc->mixpos = loopstart;
                    }
                }
            } else if (loopmode == 2) { // Bidi ping-pong loop
                if (sc->mixpos >= loopend) {
                    sc->speeddir = 1;
                    sc->mixpos = (loopend > 0) ? (loopend - 1) : 0;
                }
            }
        } else {
            if (full_pos >= step) {
                full_pos -= step;
                sc->mixpos = (uint32_t)(full_pos >> 32);
                sc->mixposlo = (uint32_t)(full_pos & 0xFFFFFFFF);
            } else {
                sc->mixpos = 0;
                sc->mixposlo = 0;
            }

            if (sc->mixpos <= loopstart) {
                sc->speeddir = 0;
                sc->mixpos = loopstart;
            }
        }
    }
}

size_t ufmod_render_frames(ufmod_t *ctx, int16_t *dest, size_t num_frames) {
    if (!ctx || ctx->finished) return 0;

    size_t frames_rendered = 0;

    while (frames_rendered < num_frames) {
        if (ctx->mixer_samplesleft <= 0) {
            if (ctx->tick == 0) {
                if (ctx->target_loops > 0 && ctx->loop_count >= ctx->target_loops) {
                    ctx->finished = 1;
                    break;
                }
                if (ctx->noloop && ctx->loop_count > 0) {
                    ctx->finished = 1;
                    break;
                }
                if (ctx->nextorder >= 0) {
                    ctx->order = ctx->nextorder;
                    ctx->nextorder = -1;
                }
                if (ctx->nextrow >= 0) {
                    ctx->row = ctx->nextrow;
                    ctx->nextrow = -1;
                }

                ufmod_do_note(ctx);

                if (ctx->nextrow < 0) {
                    int nextrow = ctx->row + 1;
                    int nextorder = -1;
                    uint8_t pat_idx = ctx->orderlist[ctx->order];
                    uint16_t rows = 64;
                    if (pat_idx < ctx->numpatternsmem && ctx->pattern[pat_idx].rows) {
                        rows = ctx->pattern[pat_idx].rows;
                    }
                    if (nextrow >= rows) {
                        nextrow = 0;
                        nextorder = ctx->order + 1;
                        if (nextorder >= ctx->numorders) {
                            ctx->loop_count++;
                            nextorder = ctx->restart;
                            if (nextorder >= ctx->numorders) {
                                nextorder = 0;
                            }
                        }
                    }
                    ctx->nextrow = nextrow;
                    if (nextorder >= 0) {
                        ctx->nextorder = nextorder;
                    }
                }
            } else {
                ufmod_do_effs(ctx);
            }

            ctx->tick++;
            if (ctx->tick >= ctx->speed + ctx->patterndelay) {
                ctx->patterndelay = 0;
                ctx->tick = 0;
            }
            ctx->mixer_samplesleft = ctx->mixer_samplespertick;
        }

        int32_t chunk = ctx->mixer_samplesleft;
        size_t frames_left = num_frames - frames_rendered;
        if ((size_t)chunk > frames_left) chunk = (int32_t)frames_left;
        if (chunk > FSOUND_BLOCK_SIZE) chunk = FSOUND_BLOCK_SIZE;

        memset(ctx->mix_buf, 0, (size_t)chunk * 2 * sizeof(int32_t));

        for (size_t ch = 0; ch < (size_t)ctx->numchannels * 2; ch++) {
            mix_channel(ctx, ch, &ctx->Channels[ch], ctx->mix_buf, chunk);
        }

        for (int32_t i = 0; i < chunk * 2; i++) {
            int32_t val = ctx->mix_buf[i];
            int32_t sign = (val < 0) ? -1 : 1;
            uint32_t u = (uint32_t)(val * sign);

            u = (u + 0x1FE0) / 0x3FC0;
            if (u > 32767) u = 32767;

            u = (u * ctx->vol_scale) >> 15;
            dest[frames_rendered * 2 + i] = (int16_t)(u * sign);
        }

        ctx->mixer_samplesleft -= chunk;
        frames_rendered += chunk;
        if (ctx->scope_buffer) ctx->scope_offset += chunk;
        ctx->time_ms += (uint32_t)(((uint64_t)chunk * 1000) / ctx->mix_rate);
    }

    return frames_rendered;
}
