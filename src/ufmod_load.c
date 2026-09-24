#include <stdlib.h>
#include <string.h>
#include "ufmod_types.h"

typedef struct {
    const uint8_t *data;
    size_t         size;
    size_t         pos;
} membuf_t;

static int mem_read(membuf_t *mb, void *dst, size_t len) {
    if (mb->pos + len > mb->size) {
        return 0;
    }
    memcpy(dst, mb->data + mb->pos, len);
    mb->pos += len;
    return 1;
}

static int mem_seek(membuf_t *mb, size_t pos) {
    if (pos > mb->size) {
        return 0;
    }
    mb->pos = pos;
    return 1;
}

static int mem_skip(membuf_t *mb, size_t count) {
    if (mb->pos + count > mb->size) {
        return 0;
    }
    mb->pos += count;
    return 1;
}

static uint8_t read_u8(membuf_t *mb) {
    uint8_t val = 0;
    mem_read(mb, &val, 1);
    return val;
}

static uint16_t read_u16(membuf_t *mb) {
    uint8_t b[2];
    if (!mem_read(mb, b, 2)) return 0;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static uint32_t read_u32(membuf_t *mb) {
    uint8_t b[4];
    if (!mem_read(mb, b, 4)) return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

void ufmod_set_bpm(ufmod_t *ctx, unsigned int bpm) {
    if (bpm == 0) return;
    ctx->defaultbpm = (uint16_t)bpm;
    ctx->mixer_samplespertick = (int32_t)((ctx->mix_rate * 5 / 2) / bpm);
}

int ufmod_load_xm(ufmod_t *ctx, const void *data, size_t size) {
    membuf_t mb = { (const uint8_t*)data, size, 0 };
    uint8_t header[60];
    if (!mem_read(&mb, header, 60)) return 0;

    if (memcmp(header, "Extended Module: ", 17) != 0 || header[37] != 0x1A) {
        return 0;
    }

    size_t ti = 0;
    for (size_t i = 0; i < 20; i++) {
        uint8_t c = header[17 + i];
        if (c >= 0x20) {
            ctx->title[ti++] = (char)c;
        }
    }
    ctx->title[ti] = '\0';

    uint32_t header_size = read_u32(&mb);
    ctx->numorders       = read_u16(&mb);
    ctx->restart         = read_u16(&mb);
    ctx->numchannels_xm  = (uint8_t)read_u16(&mb);
    ctx->numpatternsmem  = read_u16(&mb);
    ctx->numinsts        = read_u16(&mb);
    ctx->flags           = read_u16(&mb);
    ctx->defaultspeed    = read_u16(&mb);
    ctx->defaultbpm      = read_u16(&mb);

    if (ctx->numchannels_xm > 32) return 0;
    ctx->numchannels = ctx->numchannels_xm;

    memset(ctx->orderlist, 0, 256);
    size_t orders_to_read = ctx->numorders < 256 ? ctx->numorders : 256;
    if (!mem_read(&mb, ctx->orderlist, orders_to_read)) return 0;

    if (!mem_seek(&mb, 60 + header_size)) return 0;

    uint16_t max_pat = ctx->numpatternsmem;
    for (size_t i = 0; i < ctx->numorders; i++) {
        if (ctx->orderlist[i] >= max_pat) {
            max_pat = ctx->orderlist[i] + 1;
        }
    }
    ctx->numpatternsmem = max_pat;

    ctx->pattern = (FMUSIC_PATTERN*)calloc(ctx->numpatternsmem, sizeof(FMUSIC_PATTERN));
    if (!ctx->pattern) return 0;

    for (size_t i = 0; i < (size_t)ctx->numchannels; i++) {
        ctx->Channels[i * 2].speedhi = 1;
        ctx->Channels[i * 2 + 1].speedhi = 1;
        ctx->uFMOD_Ch[i].cptr = &ctx->Channels[i * 2];
    }

    ufmod_set_bpm(ctx, ctx->defaultbpm ? ctx->defaultbpm : 125);
    ctx->speed = ctx->defaultspeed ? ctx->defaultspeed : 6;
    ctx->globalvolume = 64;

    for (size_t p = 0; p < ctx->numpatternsmem; p++) {
        if (mb.pos >= mb.size) break;
        uint32_t pat_hdr_len = read_u32(&mb);
        uint8_t  pat_packing = read_u8(&mb);
        uint16_t pat_rows    = read_u16(&mb);
        uint16_t pat_data_sz = read_u16(&mb);
        (void)pat_packing;

        if (pat_hdr_len > 9) {
            mem_skip(&mb, pat_hdr_len - 9);
        }

        ctx->pattern[p].rows = pat_rows ? pat_rows : 64;
        ctx->pattern[p].patternsize = pat_data_sz;

        if (pat_data_sz > 0) {
            size_t total_notes = (size_t)ctx->pattern[p].rows * ctx->numchannels;
            ctx->pattern[p].data = (FMUSIC_NOTE*)calloc(total_notes, sizeof(FMUSIC_NOTE));
            if (!ctx->pattern[p].data) return 0;

            for (size_t n = 0; n < total_notes; n++) {
                uint8_t tag = read_u8(&mb);
                FMUSIC_NOTE *note = &ctx->pattern[p].data[n];
                if (tag & 0x80) {
                    if (tag & 0x01) note->note    = read_u8(&mb);
                    if (tag & 0x02) note->number  = read_u8(&mb);
                    if (tag & 0x04) note->uvolume = read_u8(&mb);
                    if (tag & 0x08) note->effect  = read_u8(&mb);
                    if (tag & 0x10) note->eparam  = read_u8(&mb);
                } else {
                    note->note    = tag;
                    note->number  = read_u8(&mb);
                    note->uvolume = read_u8(&mb);
                    note->effect  = read_u8(&mb);
                    note->eparam  = read_u8(&mb);
                }
            }
        }
    }

    if (ctx->numinsts > 0) {
        ctx->instrument = (FMUSIC_INSTRUMENT*)calloc(ctx->numinsts, sizeof(FMUSIC_INSTRUMENT));
        if (!ctx->instrument) return 0;

        for (size_t ins = 0; ins < ctx->numinsts; ins++) {
            if (mb.pos >= mb.size) break;
            size_t inst_start = mb.pos;
            uint32_t inst_hdr_sz = read_u32(&mb);
            mem_skip(&mb, 22);
            uint8_t  inst_type   = read_u8(&mb);
            uint16_t numsamples  = read_u16(&mb);
            (void)inst_type;

            FMUSIC_INSTRUMENT *iptr = &ctx->instrument[ins];

            if (numsamples > 0 && numsamples <= 16) {
                uint32_t samp_hdr_sz = read_u32(&mb);
                (void)samp_hdr_sz;
                mem_read(&mb, iptr->keymap, 96);
                for (size_t v = 0; v < 12; v++) {
                    iptr->VOLPoints[v * 2]     = read_u16(&mb);
                    iptr->VOLPoints[v * 2 + 1] = read_u16(&mb);
                }
                for (size_t vp = 0; vp < 12; vp++) {
                    iptr->PANPoints[vp * 2]     = read_u16(&mb);
                    iptr->PANPoints[vp * 2 + 1] = read_u16(&mb);
                }
                iptr->VOLnumpoints = read_u8(&mb);
                iptr->PANnumpoints = read_u8(&mb);
                iptr->VOLsustain   = read_u8(&mb);
                iptr->VOLLoopStart = read_u8(&mb);
                iptr->VOLLoopEnd   = read_u8(&mb);
                iptr->PANsustain   = read_u8(&mb);
                iptr->PANLoopStart = read_u8(&mb);
                iptr->PANLoopEnd   = read_u8(&mb);
                iptr->VOLtype      = read_u8(&mb);
                iptr->PANtype      = read_u8(&mb);
                iptr->VIBtype      = read_u8(&mb);
                iptr->VIBsweep     = read_u8(&mb);
                iptr->iVIBdepth    = read_u8(&mb);
                iptr->VIBrate      = read_u8(&mb);
                iptr->VOLfade      = read_u16(&mb) << 1;

                if (iptr->VOLnumpoints < 2) iptr->VOLtype = 0;
                if (iptr->PANnumpoints < 2) iptr->PANtype = 0;

                mem_seek(&mb, inst_start + inst_hdr_sz);

                for (size_t s = 0; s < numsamples; s++) {
                    FSOUND_SAMPLE *sptr = (FSOUND_SAMPLE*)calloc(1, sizeof(FSOUND_SAMPLE));
                    iptr->sample[s] = sptr;

                    sptr->length    = read_u32(&mb);
                    sptr->loopstart = read_u32(&mb);
                    sptr->looplen   = read_u32(&mb);
                    sptr->defvol    = read_u8(&mb);
                    sptr->finetune  = (int8_t)read_u8(&mb);
                    sptr->bytes     = read_u8(&mb);
                    sptr->defpan    = read_u8(&mb);
                    sptr->relative  = (int8_t)read_u8(&mb);
                    sptr->reserved  = read_u8(&mb);
                    mem_skip(&mb, 22);

                    int is_16bit = (sptr->bytes & 0x10) != 0;
                    if (is_16bit) {
                        sptr->length    >>= 1;
                        sptr->loopstart >>= 1;
                        sptr->looplen   >>= 1;
                    }

                    if (sptr->loopstart > sptr->length) {
                        sptr->loopstart = sptr->length;
                    }
                    if (sptr->loopstart + sptr->looplen > sptr->length) {
                        sptr->looplen = sptr->length - sptr->loopstart;
                    }

                    uint8_t looptype = sptr->bytes & 0x03;
                    if (looptype == 0 || sptr->looplen == 0) {
                        sptr->loopmode  = 0;
                        sptr->loopstart = 0;
                        sptr->looplen   = sptr->length;
                    } else {
                        sptr->loopmode = looptype;
                    }
                }

                for (size_t s = 0; s < numsamples; s++) {
                    FSOUND_SAMPLE *sptr = iptr->sample[s];
                    if (!sptr || sptr->length == 0) continue;

                    int is_16bit = (sptr->bytes & 0x10) != 0;
                    sptr->buff = (int16_t*)calloc(sptr->length + 4, sizeof(int16_t));
                    if (!sptr->buff) return 0;

                    int16_t delta = 0;
                    if (!is_16bit) {
                        for (size_t b = 0; b < sptr->length; b++) {
                            int8_t val = (int8_t)read_u8(&mb);
                            delta = (int16_t)(delta + (val << 8));
                            sptr->buff[b] = delta;
                        }
                    } else {
                        for (size_t b = 0; b < sptr->length; b++) {
                            int16_t val = (int16_t)read_u16(&mb);
                            delta = (int16_t)(delta + val);
                            sptr->buff[b] = delta;
                        }
                    }

                    if (sptr->loopmode == 1) {
                        sptr->buff[sptr->loopstart + sptr->looplen] = sptr->buff[sptr->loopstart];
                    } else if (sptr->loopmode == 2) {
                        sptr->buff[sptr->loopstart + sptr->looplen] = sptr->buff[sptr->loopstart + sptr->looplen - 1];
                    }
                }
            } else {
                mem_seek(&mb, inst_start + inst_hdr_sz);
            }
        }
    }

    return 1;
}
