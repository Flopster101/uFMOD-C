#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ufmod_types.h"
#include "ufmod_tables.h"

void ufmod_set_bpm(ufmod_t *ctx, unsigned int bpm);

static void process_envelope(uF_MOD_CHANNEL *cptr, FMUSIC_INSTRUMENT *iptr, int is_pan) {
    uint8_t  type, numpoints, sustain, loopstart, loopend;
    const uint16_t *points;
    int32_t *pos, *tick, *delta, *frac, *val;
    uint8_t *stopped;

    if (is_pan) {
        type = iptr->PANtype;
        numpoints = iptr->PANnumpoints;
        sustain = iptr->PANsustain;
        loopstart = iptr->PANLoopStart;
        loopend = iptr->PANLoopEnd;
        points = iptr->PANPoints;
        pos = &cptr->envpanpos;
        tick = &cptr->envpantick;
        delta = &cptr->envpandelta;
        frac = &cptr->envpanfrac;
        val = &cptr->envpan;
        stopped = &cptr->envpanstopped;
        cptr->notectrl |= FMUSIC_PAN;
    } else {
        type = iptr->VOLtype;
        numpoints = iptr->VOLnumpoints;
        sustain = iptr->VOLsustain;
        loopstart = iptr->VOLLoopStart;
        loopend = iptr->VOLLoopEnd;
        points = iptr->VOLPoints;
        pos = &cptr->envvolpos;
        tick = &cptr->envvoltick;
        delta = &cptr->envvoldelta;
        frac = &cptr->envvolfrac;
        val = &cptr->envvol;
        stopped = &cptr->envvolstopped;
        cptr->notectrl |= FMUSIC_VOLUME;
    }

    if (*stopped) return;
    if ((uint32_t)*pos >= numpoints) return;

    if (*tick == points[(*pos) * 2]) {
        if ((type & FMUSIC_ENVELOPE_LOOP) && *pos == loopend) {
            *pos = loopstart;
            *tick = points[(*pos) * 2];
        }

        uint32_t curpos = *pos;
        uint16_t currtick = points[curpos * 2];
        uint16_t currval  = points[curpos * 2 + 1];

        if (curpos >= (uint32_t)numpoints - 1) {
            *val = currval;
            *stopped = 1;
            return;
        }

        uint16_t nexttick = points[(curpos + 1) * 2];
        uint16_t nextval  = points[(curpos + 1) * 2 + 1];

        *val = currval;

        if ((type & FMUSIC_ENVELOPE_SUSTAIN) && curpos == sustain && !cptr->keyoff) {
            return;
        }

        (*pos)++;
        *frac = (int32_t)currval << 16;
        if (nexttick > currtick) {
            *delta = ((int32_t)(nextval - currval) << 16) / (int32_t)(nexttick - currtick);
        } else {
            *delta = 0;
        }
    } else {
        *frac += *delta;
    }

    (*tick)++;
    *val = *frac >> 16;
}

static int vibrato_or_tremolo(uint8_t *pos, uint8_t speed, uint8_t depth, uint8_t wavecontrol) {
    uint8_t p = *pos;
    *pos = (p + speed) & 0x3F;

    int delta;
    switch (wavecontrol & 3) {
        case 0: { // Sine
            int idx = p & 0x1F;
            delta = uFMOD_sin127[idx];
            if (p & 0x20) {
                delta = -delta;
            }
            break;
        }
        case 1: { // Ramp down
            delta = 127 - (p * 4);
            break;
        }
        case 2: // Square wave
            delta = (p < 32) ? 127 : -127;
            break;
        default:
            delta = 0;
            break;
    }

    return (delta * (int)(int8_t)depth);
}

static void apply_vibrato(uF_MOD_CHANNEL *cptr) {
    int delta = vibrato_or_tremolo(&cptr->vibpos, cptr->vibspeed, cptr->vibdepth, cptr->wavecontrol & 3);
    cptr->freqdelta = delta >> 6;
    cptr->notectrl |= FMUSIC_FREQ;
}

static void apply_tremolo(uF_MOD_CHANNEL *cptr) {
    int delta = vibrato_or_tremolo(&cptr->tremolopos, cptr->tremolospeed, cptr->tremolodepth, (cptr->wavecontrol >> 4) & 3);
    cptr->voldelta = delta >> 5;
    cptr->notectrl |= FMUSIC_VOLUME;
}

static void apply_portamento(uF_MOD_CHANNEL *cptr) {
    cptr->notectrl |= FMUSIC_FREQ;
    int32_t freq = cptr->freq;
    int32_t target = cptr->portatarget;
    int32_t speed = (int32_t)cptr->portaspeed << 2;
    int32_t diff = freq - target;
    if (diff > 0) {
        diff -= speed;
        if (diff < 0) {
            cptr->freq = target;
        } else {
            cptr->freq = target + diff;
        }
    } else {
        diff += speed;
        if (diff > 0) {
            cptr->freq = target;
        } else {
            cptr->freq = target + diff;
        }
    }
}

static void apply_volbyte(uF_MOD_CHANNEL *cptr, uint8_t vol) {
    if (vol >= 0x10 && vol <= 0x50) {
        cptr->volume = vol - 0x10;
        cptr->notectrl |= FMUSIC_VOLUME;
        return;
    }

    uint8_t hi = vol >> 4;
    uint8_t lo = vol & 0x0F;

    switch (hi) {
        case 6: // Vol slide down
            cptr->volume -= lo;
            if (cptr->volume < 0) cptr->volume = 0;
            cptr->notectrl |= FMUSIC_VOLUME;
            break;
        case 7: // Vol slide up
            cptr->volume += lo;
            if (cptr->volume > 64) cptr->volume = 64;
            cptr->notectrl |= FMUSIC_VOLUME;
            break;
        case 8: // Fine vol slide down
            cptr->volume -= lo;
            if (cptr->volume < 0) cptr->volume = 0;
            cptr->notectrl |= FMUSIC_VOLUME;
            break;
        case 9: // Fine vol slide up
            cptr->volume += lo;
            if (cptr->volume > 64) cptr->volume = 64;
            cptr->notectrl |= FMUSIC_VOLUME;
            break;
        case 0xA: // Vibrato speed
            if (lo) cptr->vibspeed = lo;
            break;
        case 0xB: // Vibrato depth
            if (lo) cptr->vibdepth = lo;
            break;
        case 0xC: // Set panning
            cptr->pan = lo << 4;
            cptr->notectrl |= FMUSIC_PAN;
            break;
        case 0xD: // Pan slide left
            cptr->pan -= lo;
            if (cptr->pan < 0) cptr->pan = 0;
            cptr->notectrl |= FMUSIC_PAN;
            break;
        case 0xE: // Pan slide right
            cptr->pan += lo;
            if (cptr->pan > 255) cptr->pan = 255;
            cptr->notectrl |= FMUSIC_PAN;
            break;
        case 0xF: // Tone portamento
            if (lo) cptr->portaspeed = lo << 4;
            cptr->notectrl &= ~(FMUSIC_TRIGGER | FMUSIC_FREQ);
            cptr->portatarget = cptr->period;
            break;
    }
}

static void update_channel_sound(ufmod_t *ctx, uF_MOD_CHANNEL *cptr, FSOUND_SAMPLE *sptr) {
    FSOUND_CHANNEL *sc = cptr->cptr;
    if (!sc) return;

    if (cptr->notectrl & FMUSIC_TRIGGER) {
        cptr->notectrl &= ~FMUSIC_TRIGGER;

        if (sc->fsptr != NULL) {
            ptrdiff_t ch_idx = sc - ctx->Channels;
            ptrdiff_t other_idx = ch_idx ^ 1;
            FSOUND_CHANNEL *other = &ctx->Channels[other_idx];

            *other = *sc;
            cptr->cptr = other;

            sc->actualvolume = 0;
            sc->leftvolume = 0;
            sc->rightvolume = 0;

            sc = other;
        }

        sc->fsptr = sptr;
        sc->mixposlo = 0;
        sc->ramp_leftvolume = 0;
        sc->ramp_rightvolume = 0;
        sc->ramp_count = 0;
        sc->speeddir = 0;
        sc->mixpos = (uint32_t)sc->fsampleoffset;
        sc->fsampleoffset = 0;
    }

    if (cptr->notectrl & FMUSIC_VOLUME) {
        int vol = cptr->volume + cptr->voldelta;
        if (vol < 0) vol = 0;
        if (vol > 64) vol = 64;

#if UFMOD_GLOBALVOL_ON
        int32_t v = vol * 255 * ctx->globalvolume;
#else
        int32_t v = vol * 255 * 64;
#endif
        if (v > 0xFF000) v = 0xFF000;

        uint64_t full_vol = ((uint64_t)v * (uint32_t)cptr->envvol) >> 6;
        full_vol = ((uint64_t)full_vol * (uint32_t)cptr->fadeoutvol) >> 16;
        int32_t actualvol = (int32_t)(full_vol >> 13);
        if (actualvol > 127) actualvol = 127;
        if (actualvol < 0) actualvol = 0;
        sc->actualvolume = actualvol;

        sc->leftvolume = (sc->actualvolume * sc->actualpan) / 255;
        sc->rightvolume = (sc->actualvolume * (255 - sc->actualpan)) / 255;
    }

    if (cptr->notectrl & FMUSIC_PAN) {
        int pan = cptr->pan;
        int dist = 128 - abs(pan - 128);
        int envpan = cptr->envpan;
        int finalpan = pan + ((envpan - 32) * (dist >> 5));
        if (finalpan < 0) finalpan = 0;
        if (finalpan > 255) finalpan = 255;
        sc->actualpan = finalpan;
        sc->leftvolume = (sc->actualvolume * finalpan) / 255;
        sc->rightvolume = (sc->actualvolume * (255 - finalpan)) / 255;
    }

    if (cptr->notectrl & FMUSIC_FREQ) {
        int32_t effective_period = cptr->freq - cptr->freqdelta;
        if (effective_period < 1) effective_period = 1;

        uint32_t freq;
        if (ctx->flags & 1) { // Linear periods
            double p = (4608.0 - (double)effective_period) / 768.0;
            freq = (uint32_t)(8363.0 * pow(2.0, p));
        } else { // Amiga periods
            freq = (uint32_t)(0xDA7790 / effective_period);
        }

        if (freq < 40) freq = 40;
        uint64_t full_speed = ((uint64_t)freq << 32) / ctx->mix_rate;
        sc->speedhi = (uint32_t)(full_speed >> 32);
        sc->speedlo = (uint32_t)(full_speed & 0xFFFFFFFF);
    }
}

void ufmod_do_note(ufmod_t *ctx) {
    if (ctx->order >= ctx->numorders) return;
    uint8_t pat_idx = ctx->orderlist[ctx->order];
    if (pat_idx >= ctx->numpatternsmem) return;

    FMUSIC_PATTERN *pat = &ctx->pattern[pat_idx];
    if (!pat->data || ctx->row >= pat->rows) return;

    FMUSIC_NOTE *notes = &pat->data[ctx->row * ctx->numchannels];

    for (size_t ch = 0; ch < (size_t)ctx->numchannels; ch++) {
        FMUSIC_NOTE *n = &notes[ch];
        uF_MOD_CHANNEL *cptr = &ctx->uFMOD_Ch[ch];
        int porta = (n->effect == FMUSIC_XM_PORTATO || n->effect == FMUSIC_XM_PORTATOVOLSLIDE ||
                     (n->uvolume >> 4) == 0x0F);

        if (n->number > 0) {
            cptr->inst = n->number - 1;
        }
        if (n->note > 0 && n->note < 97) {
            cptr->note = n->note - 1;
        }

        FMUSIC_INSTRUMENT *iptr = NULL;
        FSOUND_SAMPLE *sptr = NULL;
        if (ctx->instrument && cptr->inst < ctx->numinsts) {
            iptr = &ctx->instrument[cptr->inst];
            uint8_t samp_idx = iptr->keymap[cptr->note < 96 ? cptr->note : 0];
            if (samp_idx < 16) {
                sptr = iptr->sample[samp_idx];
            }
        }

        if (n->effect != FMUSIC_XM_TREMOLO && cptr->recenteffect == FMUSIC_XM_TREMOLO) {
            cptr->volume += cptr->voldelta;
        }
        cptr->recenteffect = n->effect;
        cptr->voldelta = 0;
        cptr->freqdelta = 0;
        cptr->notectrl = FMUSIC_VOLUME | FMUSIC_FREQ;

        if (n->note > 0 && n->note < 97 && sptr) {
            int realnote = (int)cptr->note + (int)sptr->relative;
            cptr->realnote = (uint8_t)realnote;

            int period;
            if (ctx->flags & 1) { // Linear periods
                period = 7680 - (realnote << 6) - ((int)sptr->finetune >> 1);
            } else { // Amiga periods
                period = 0;
            }

            cptr->period = period;
            if (!porta) {
                cptr->freq = period;
            }
            cptr->notectrl |= FMUSIC_TRIGGER;
        }

        if (n->number > 0 && sptr) {
            cptr->volume = sptr->defvol;
            cptr->pan = sptr->defpan;
            cptr->envvol = 64;
            cptr->envpan = 32;
            cptr->fadeoutvol = 65536;
            cptr->envvoltick = 0;
            cptr->envvolpos = 0;
            cptr->envvoldelta = 0;
            cptr->envvolstopped = 0;
            cptr->envpantick = 0;
            cptr->envpanpos = 0;
            cptr->envpandelta = 0;
            cptr->envpanstopped = 0;
            cptr->keyoff = 0;

            if ((cptr->wavecontrol & 0xF0) < 0x50) cptr->tremolopos = 0;
            if ((cptr->wavecontrol & 0x0F) < 0x05) cptr->vibpos = 0;

            cptr->notectrl |= FMUSIC_VOLUME | FMUSIC_PAN;
        }

#if UFMOD_VOLUMEBYTE_ON
        if (n->uvolume > 0) {
            apply_volbyte(cptr, n->uvolume);
        }
#endif

#if UFMOD_KEYOFF_ON
        if (n->note == 97 || n->effect == FMUSIC_XM_KEYOFF) {
            cptr->keyoff = 1;
        }
#endif

#if UFMOD_VOLUMEENVELOPE_ON
        if (iptr && (iptr->VOLtype & 1)) {
            process_envelope(cptr, iptr, 0);
        } else if (cptr->keyoff) {
            cptr->envvol = 0;
        }
#else
        if (cptr->keyoff) {
            cptr->envvol = 0;
        }
#endif

#if UFMOD_PANENVELOPE_ON
        if (iptr && (iptr->PANtype & 1)) {
            process_envelope(cptr, iptr, 1);
        }
#endif

        uint8_t param = n->eparam;
        switch (n->effect) {
#if UFMOD_PORTAUP_OR_DOWN_ON
            case FMUSIC_XM_PORTAUP:
            case FMUSIC_XM_PORTADOWN:
                if (param) cptr->portaupdown = param;
                break;
#endif
#if UFMOD_PORTATO_ON
            case FMUSIC_XM_PORTATO:
                if (param) cptr->portaspeed = param;
                cptr->portatarget = cptr->period;
                cptr->notectrl &= ~FMUSIC_TRIGGER;
                break;
#endif
#if UFMOD_PORTATOVOLSLIDE_ON
            case FMUSIC_XM_PORTATOVOLSLIDE:
                if (param) cptr->volslide = param;
                cptr->portatarget = cptr->period;
                cptr->notectrl &= ~FMUSIC_TRIGGER;
                break;
#endif
#if UFMOD_VIBRATO_ON
            case FMUSIC_XM_VIBRATO:
                if (param >> 4) cptr->vibspeed = param >> 4;
                if (param & 0x0F) cptr->vibdepth = param & 0x0F;
                break;
#endif
#if UFMOD_VIBRATOVOLSLIDE_ON
            case FMUSIC_XM_VIBRATOVOLSLIDE:
                if (param) cptr->volslide = param;
                break;
#endif
#if UFMOD_TREMOLO_ON
            case FMUSIC_XM_TREMOLO:
                if (param >> 4) cptr->tremolospeed = param >> 4;
                if (param & 0x0F) cptr->tremolodepth = param & 0x0F;
                break;
#endif
#if UFMOD_SETSAMPLEOFFSET_ON
            case FMUSIC_XM_SETSAMPLEOFFSET:
                if (param) cptr->sampleoffset = (int32_t)param << 8;
                cptr->cptr->fsampleoffset = cptr->sampleoffset;
                break;
#endif
#if UFMOD_VOLUMESLIDE_ON
            case FMUSIC_XM_VOLUMESLIDE:
                if (param) cptr->volslide = param;
                break;
#endif
#if UFMOD_PATTERNJUMP_ON
            case FMUSIC_XM_PATTERNJUMP:
                ctx->nextorder = param;
                ctx->nextrow = 0;
                break;
#endif
#if UFMOD_SETVOLUME_ON
            case FMUSIC_XM_SETVOLUME:
                cptr->volume = param > 64 ? 64 : param;
                cptr->notectrl |= FMUSIC_VOLUME;
                break;
#endif
#if UFMOD_PATTERNBREAK_ON
            case FMUSIC_XM_PATTERNBREAK:
                ctx->nextorder = (ctx->nextorder >= 0) ? ctx->nextorder : ctx->order + 1;
                ctx->nextrow = (param >> 4) * 10 + (param & 0x0F);
                break;
#endif
#if UFMOD_SETSPEED_ON
            case FMUSIC_XM_SETSPEED:
                if (param < 0x20) {
                    ctx->speed = param ? param : 1;
                } else {
                    ufmod_set_bpm(ctx, param);
                }
                break;
#endif
#if UFMOD_SETGLOBALVOLUME_ON
            case FMUSIC_XM_SETGLOBALVOLUME:
                ctx->globalvolume = param > 64 ? 64 : param;
                break;
#endif
#if UFMOD_GLOBALVOLSLIDE_ON
            case FMUSIC_XM_GLOBALVOLSLIDE:
                if (param) ctx->globalvsl = param;
                break;
#endif
#if UFMOD_PANSLIDE_ON
            case FMUSIC_XM_PANSLIDE:
                if (param) cptr->panslide = param;
                break;
#endif
#if UFMOD_SETPANPOSITION_ON
            case FMUSIC_XM_SETPANPOSITION:
                cptr->pan = param;
                cptr->notectrl |= FMUSIC_PAN;
                break;
#endif
            case FMUSIC_XM_SPECIAL: {
                uint8_t cmd = param >> 4;
                uint8_t p = param & 0x0F;
                switch (cmd) {
#if UFMOD_FINEPORTAUP_ON
                    case FMUSIC_XM_FINEPORTAUP:
                        if (p) cptr->fineportaup = p;
                        cptr->freq -= (int32_t)cptr->fineportaup << 2;
                        if (cptr->freq < 1) cptr->freq = 1;
                        cptr->notectrl |= FMUSIC_FREQ;
                        break;
#endif
#if UFMOD_FINEPORTADOWN_ON
                    case FMUSIC_XM_FINEPORTADOWN:
                        if (p) cptr->fineportadown = p;
                        cptr->freq += (int32_t)cptr->fineportadown << 2;
                        cptr->notectrl |= FMUSIC_FREQ;
                        break;
#endif
#if UFMOD_SETVIBRATOWAVE_ON
                    case FMUSIC_XM_SETVIBRATOWAVE:
                        cptr->wavecontrol = (cptr->wavecontrol & 0xF0) | (p & 0x0F);
                        break;
#endif
#if UFMOD_SETTREMOLOWAVE_ON
                    case FMUSIC_XM_SETTREMOLOWAVE:
                        cptr->wavecontrol = (cptr->wavecontrol & 0x0F) | ((p & 0x0F) << 4);
                        break;
#endif
#if UFMOD_PATTERNDELAY_ON
                    case FMUSIC_XM_PATTERNDELAY:
                        ctx->patterndelay = p;
                        break;
#endif
                }
                break;
            }
        }

        update_channel_sound(ctx, cptr, sptr);
    }
}

void ufmod_do_effs(ufmod_t *ctx) {
    if (ctx->order >= ctx->numorders) return;
    uint8_t pat_idx = ctx->orderlist[ctx->order];
    if (pat_idx >= ctx->numpatternsmem) return;

    FMUSIC_PATTERN *pat = &ctx->pattern[pat_idx];
    if (!pat->data || ctx->row >= pat->rows) return;

    FMUSIC_NOTE *notes = &pat->data[ctx->row * ctx->numchannels];

    for (size_t ch = 0; ch < (size_t)ctx->numchannels; ch++) {
        FMUSIC_NOTE *n = &notes[ch];
        uF_MOD_CHANNEL *cptr = &ctx->uFMOD_Ch[ch];

        FMUSIC_INSTRUMENT *iptr = NULL;
        FSOUND_SAMPLE *sptr = NULL;
        if (ctx->instrument && cptr->inst < ctx->numinsts) {
            iptr = &ctx->instrument[cptr->inst];
            uint8_t samp_idx = iptr->keymap[cptr->note < 96 ? cptr->note : 0];
            if (samp_idx < 16) {
                sptr = iptr->sample[samp_idx];
            }
        }

        cptr->voldelta = 0;
        cptr->freqdelta = 0;
        cptr->notectrl = 0;

#if UFMOD_VOLUMEENVELOPE_ON
        if (iptr && (iptr->VOLtype & 1)) {
            process_envelope(cptr, iptr, 0);
        }
#endif
#if UFMOD_PANENVELOPE_ON
        if (iptr && (iptr->PANtype & 1)) {
            process_envelope(cptr, iptr, 1);
        }
#endif

        if (cptr->keyoff && iptr) {
            if (cptr->fadeoutvol > iptr->VOLfade) {
                cptr->fadeoutvol -= iptr->VOLfade;
            } else {
                cptr->fadeoutvol = 0;
            }
            cptr->notectrl |= FMUSIC_VOLUME;
        }

#if UFMOD_VOLUMEBYTE_ON
        if (n->uvolume > 0) {
            uint8_t hi = n->uvolume >> 4;
            uint8_t lo = n->uvolume & 0x0F;
            if (hi == 6) { // Vol slide down
                cptr->volume -= lo;
                if (cptr->volume < 0) cptr->volume = 0;
                cptr->notectrl |= FMUSIC_VOLUME;
            } else if (hi == 7) { // Vol slide up
                cptr->volume += lo;
                if (cptr->volume > 64) cptr->volume = 64;
                cptr->notectrl |= FMUSIC_VOLUME;
            }
        }
#endif

        uint8_t param = n->eparam;
        switch (n->effect) {
#if UFMOD_ARPEGGIO_ON
            case 0x00: // Arpeggio
                if (param) {
                    uint8_t step = ctx->tick % 3;
                    uint8_t semitones = 0;
                    if (step == 1) semitones = param >> 4;
                    else if (step == 2) semitones = param & 0x0F;
                    cptr->freqdelta = (int32_t)semitones << 6;
                    cptr->notectrl |= FMUSIC_FREQ;
                }
                break;
#endif
#if UFMOD_PORTAUP_ON
            case FMUSIC_XM_PORTAUP:
                cptr->freq -= (int32_t)cptr->portaupdown << 2;
                if (cptr->freq < 1) cptr->freq = 1;
                cptr->freqdelta = 0;
                cptr->notectrl |= FMUSIC_FREQ;
                break;
#endif
#if UFMOD_PORTADOWN_ON
            case FMUSIC_XM_PORTADOWN:
                cptr->freq += (int32_t)cptr->portaupdown << 2;
                cptr->freqdelta = 0;
                cptr->notectrl |= FMUSIC_FREQ;
                break;
#endif
#if UFMOD_PORTATO_ON
            case FMUSIC_XM_PORTATO:
                apply_portamento(cptr);
                break;
#endif
#if UFMOD_VIBRATO_ON
            case FMUSIC_XM_VIBRATO:
                apply_vibrato(cptr);
                break;
#endif
#if UFMOD_PORTATOVOLSLIDE_ON
            case FMUSIC_XM_PORTATOVOLSLIDE:
                apply_portamento(cptr);
                if (cptr->volslide >> 4) {
                    cptr->volume += (cptr->volslide >> 4);
                    if (cptr->volume > 64) cptr->volume = 64;
                } else {
                    cptr->volume -= (cptr->volslide & 0x0F);
                    if (cptr->volume < 0) cptr->volume = 0;
                }
                cptr->notectrl |= FMUSIC_VOLUME;
                break;
#endif
#if UFMOD_VIBRATOVOLSLIDE_ON
            case FMUSIC_XM_VIBRATOVOLSLIDE:
                apply_vibrato(cptr);
                if (cptr->volslide >> 4) {
                    cptr->volume += (cptr->volslide >> 4);
                    if (cptr->volume > 64) cptr->volume = 64;
                } else {
                    cptr->volume -= (cptr->volslide & 0x0F);
                    if (cptr->volume < 0) cptr->volume = 0;
                }
                cptr->notectrl |= FMUSIC_VOLUME;
                break;
#endif
#if UFMOD_TREMOLO_ON
            case FMUSIC_XM_TREMOLO:
                apply_tremolo(cptr);
                break;
#endif
#if UFMOD_VOLUMESLIDE_ON
            case FMUSIC_XM_VOLUMESLIDE:
                if (cptr->volslide >> 4) {
                    cptr->volume += (cptr->volslide >> 4);
                    if (cptr->volume > 64) cptr->volume = 64;
                } else {
                    cptr->volume -= (cptr->volslide & 0x0F);
                    if (cptr->volume < 0) cptr->volume = 0;
                }
                cptr->notectrl |= FMUSIC_VOLUME;
                break;
#endif
#if UFMOD_GLOBALVOLSLIDE_ON
            case FMUSIC_XM_GLOBALVOLSLIDE:
                if (ctx->globalvsl >> 4) {
                    ctx->globalvolume += (ctx->globalvsl >> 4);
#if UFMOD_RUNTIME_QUIRKS
                    if (!(ctx->quirk_flags & UFMOD_QUIRK_UNCLAMPED_GLOBAL_VOLSLIDE) && ctx->globalvolume > 64) {
                        ctx->globalvolume = 64;
                    }
#else
#if UFMOD_SETGLOBALVOLUME_ON
                    if (ctx->globalvolume > 64) ctx->globalvolume = 64;
#endif
#endif
                } else {
                    ctx->globalvolume -= (ctx->globalvsl & 0x0F);
                    if (ctx->globalvolume < 0) ctx->globalvolume = 0;
                }
                break;
#endif
#if UFMOD_PANSLIDE_ON
            case FMUSIC_XM_PANSLIDE:
                if (cptr->panslide >> 4) {
                    cptr->pan += (cptr->panslide >> 4);
                    if (cptr->pan > 255) cptr->pan = 255;
                } else {
                    cptr->pan -= (cptr->panslide & 0x0F);
                    if (cptr->pan < 0) cptr->pan = 0;
                }
                cptr->notectrl |= FMUSIC_PAN;
                break;
#endif
            case FMUSIC_XM_SPECIAL: {
                uint8_t cmd = param >> 4;
                uint8_t p = param & 0x0F;
#if UFMOD_NOTECUT_ON
                if (cmd == FMUSIC_XM_NOTECUT && ctx->tick == p) {
                    cptr->volume = 0;
                    cptr->notectrl |= FMUSIC_VOLUME;
                }
#endif
                break;
            }
        }

        update_channel_sound(ctx, cptr, sptr);
    }
}
