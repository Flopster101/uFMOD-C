#ifndef UFMOD_TYPES_H
#define UFMOD_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include "ufmod.h"

#define FSOUND_BLOCK_SIZE 1024
#define FSOUND_RAMP_STEPS 128
#define FSOUND_RAMP_POW   7

#define FMUSIC_ENVELOPE_SUSTAIN 2
#define FMUSIC_ENVELOPE_LOOP    4
#define FMUSIC_FREQ             1
#define FMUSIC_VOLUME           2
#define FMUSIC_PAN              4
#define FMUSIC_TRIGGER          8

enum {
    FMUSIC_XM_PORTAUP         = 1,
    FMUSIC_XM_PORTADOWN       = 2,
    FMUSIC_XM_PORTATO         = 3,
    FMUSIC_XM_VIBRATO         = 4,
    FMUSIC_XM_PORTATOVOLSLIDE = 5,
    FMUSIC_XM_VIBRATOVOLSLIDE = 6,
    FMUSIC_XM_TREMOLO         = 7,
    FMUSIC_XM_SETPANPOSITION  = 8,
    FMUSIC_XM_SETSAMPLEOFFSET = 9,
    FMUSIC_XM_VOLUMESLIDE     = 10,
    FMUSIC_XM_PATTERNJUMP     = 11,
    FMUSIC_XM_SETVOLUME       = 12,
    FMUSIC_XM_PATTERNBREAK    = 13,
    FMUSIC_XM_SPECIAL         = 14,
    FMUSIC_XM_SETSPEED        = 15,
    FMUSIC_XM_SETGLOBALVOLUME = 16,
    FMUSIC_XM_GLOBALVOLSLIDE  = 17,
    FMUSIC_XM_KEYOFF          = 20,
    FMUSIC_XM_PANSLIDE        = 25,
    FMUSIC_XM_MULTIRETRIG     = 27,
    FMUSIC_XM_TREMOR          = 29,
    FMUSIC_XM_EXTRAFINEPORTA  = 33
};

enum {
    FMUSIC_XM_FINEPORTAUP      = 1,
    FMUSIC_XM_FINEPORTADOWN    = 2,
    FMUSIC_XM_SETGLISSANDO     = 3,
    FMUSIC_XM_SETVIBRATOWAVE   = 4,
    FMUSIC_XM_SETFINETUNE      = 5,
    FMUSIC_XM_PATTERNLOOP      = 6,
    FMUSIC_XM_SETTREMOLOWAVE   = 7,
    FMUSIC_XM_SETPANPOSITION16 = 8,
    FMUSIC_XM_RETRIG           = 9,
    FMUSIC_XM_NOTECUT          = 12,
    FMUSIC_XM_NOTEDELAY        = 13,
    FMUSIC_XM_PATTERNDELAY     = 14
};

typedef struct {
    uint32_t length;
    uint32_t loopstart;
    uint32_t looplen;
    uint8_t  defvol;
    int8_t   finetune;
    uint8_t  bytes;
    uint8_t  defpan;
    int8_t   relative;
    uint8_t  reserved;
    uint8_t  loopmode;
    uint8_t  align;
    int16_t *buff;
} FSOUND_SAMPLE;

typedef struct {
    int32_t        actualvolume;
    int32_t        actualpan;
    int32_t        fsampleoffset;
    int32_t        leftvolume;
    int32_t        rightvolume;
    uint32_t       mixpos;
    uint32_t       speedlo;
    uint32_t       speedhi;
    int32_t        ramp_LR_target;
    int32_t        ramp_leftspeed;
    int32_t        ramp_rightspeed;
    FSOUND_SAMPLE *fsptr;
    uint32_t       mixposlo;
    int32_t        ramp_leftvolume;
    int32_t        ramp_rightvolume;
    uint16_t       ramp_count;
    uint8_t        speeddir;
    uint8_t        pad;
} FSOUND_CHANNEL;

typedef struct {
    uint8_t note;
    uint8_t number;
    uint8_t uvolume;
    uint8_t effect;
    uint8_t eparam;
} FMUSIC_NOTE;

typedef struct {
    uint16_t     rows;
    uint16_t     patternsize;
    FMUSIC_NOTE *data;
} FMUSIC_PATTERN;

typedef struct {
    FSOUND_SAMPLE *sample[16];
    uint8_t        keymap[96];
    uint16_t       VOLPoints[24];
    uint16_t       PANPoints[24];
    uint8_t        VOLnumpoints;
    uint8_t        PANnumpoints;
    uint8_t        VOLsustain;
    uint8_t        VOLLoopStart;
    uint8_t        VOLLoopEnd;
    uint8_t        PANsustain;
    uint8_t        PANLoopStart;
    uint8_t        PANLoopEnd;
    uint8_t        VOLtype;
    uint8_t        PANtype;
    uint8_t        VIBtype;
    uint8_t        VIBsweep;
    uint8_t        iVIBdepth;
    uint8_t        VIBrate;
    uint16_t       VOLfade;
} FMUSIC_INSTRUMENT;

typedef struct {
    uint8_t  note;
    uint8_t  samp;
    uint8_t  notectrl;
    uint8_t  inst;
    FSOUND_CHANNEL *cptr;
    int32_t  freq;
    int32_t  volume;
    int32_t  voldelta;
    int32_t  freqdelta;
    int32_t  pan;
    int32_t  envvoltick;
    int32_t  envvolpos;
    int32_t  envvoldelta;
    int32_t  envpantick;
    int32_t  envpanpos;
    int32_t  envpandelta;
    int32_t  ivibsweeppos;
    int32_t  ivibpos;
    uint16_t keyoff;
    uint8_t  envvolstopped;
    uint8_t  envpanstopped;
    int32_t  envvolfrac;
    int32_t  envvol;
    int32_t  fadeoutvol;
    int32_t  envpanfrac;
    int32_t  envpan;
    int32_t  period;
    int32_t  sampleoffset;
    int32_t  portatarget;
    int32_t  patloopno;
    int32_t  patlooprow;
    uint8_t  realnote;
    uint8_t  recenteffect;
    uint8_t  portaupdown;
    uint8_t  unused;
    uint8_t  xtraportadown;
    uint8_t  xtraportaup;
    uint8_t  volslide;
    uint8_t  panslide;
    uint8_t  retrigx;
    uint8_t  retrigy;
    uint8_t  portaspeed;
    uint8_t  vibpos;
    uint8_t  vibspeed;
    uint8_t  vibdepth;
    uint8_t  tremolopos;
    uint8_t  tremolospeed;
    uint8_t  tremolodepth;
    uint8_t  tremorpos;
    uint8_t  tremoron;
    uint8_t  tremoroff;
    uint8_t  wavecontrol;
    uint8_t  finevslup;
    uint8_t  fineportaup;
    uint8_t  fineportadown;
} uF_MOD_CHANNEL;

struct ufmod_context {
    FMUSIC_PATTERN    *pattern;
    FMUSIC_INSTRUMENT *instrument;
    int32_t            mixer_samplesleft;
    int32_t            globalvolume;
    int32_t            tick;
    int32_t            speed;
    int32_t            order;
    int32_t            row;
    int32_t            patterndelay;
    int32_t            nextorder;
    int32_t            nextrow;
    int32_t            unused1;
    int32_t            numchannels;
    FSOUND_CHANNEL     Channels[64];
    uF_MOD_CHANNEL     uFMOD_Ch[32];
    int32_t            mixer_samplespertick;
    uint16_t           numorders;
    uint16_t           restart;
    uint8_t            numchannels_xm;
    uint8_t            globalvsl;
    uint16_t           numpatternsmem;
    uint16_t           numinsts;
    uint16_t           flags;
    uint16_t           defaultspeed;
    uint16_t           defaultbpm;
    uint8_t            orderlist[256];

    uint32_t           mix_rate;
    uint32_t           vol_scale;
    uint32_t           time_ms;
    int                noloop;
    int                finished;
    int                loop_count;
    int                target_loops;
    char               title[32];

    int32_t            mix_buf[FSOUND_BLOCK_SIZE * 2];
};

#endif
