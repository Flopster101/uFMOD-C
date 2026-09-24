#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "ufmod.h"

static void write_wav_header(FILE *f, uint32_t sample_rate, uint32_t num_samples) {
    uint32_t byte_rate = sample_rate * 2 * sizeof(int16_t);
    uint16_t block_align = 2 * sizeof(int16_t);
    uint32_t data_size = num_samples * block_align;
    uint32_t riff_size = 36 + data_size;

    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);

    uint32_t subchunk1_size = 16;
    uint16_t audio_format = 1;
    uint16_t num_channels = 2;
    uint16_t bits_per_sample = 16;

    fwrite(&subchunk1_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&num_channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);

    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.xm> <output.wav> [duration_seconds]\n", argv[0]);
        return 1;
    }

    const char *in_path = argv[1];
    const char *out_path = argv[2];
    double duration = 0.0;
    int target_loops = 0;
    unsigned int quirks = 0xFFFFFFFF;

    for (int i = 3; i < argc; i++) {
        if ((strcmp(argv[i], "--loops") == 0 || strcmp(argv[i], "-l") == 0) && i + 1 < argc) {
            target_loops = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--quirks") == 0 && i + 1 < argc) {
            const char *q = argv[++i];
            if (strcmp(q, "none") == 0) quirks = UFMOD_QUIRK_NONE;
            else if (strcmp(q, "skidrow") == 0) quirks = UFMOD_QUIRK_SKIDROW_LAUNCHER;
            else if (strcmp(q, "persist") == 0) quirks = UFMOD_QUIRK_PERSIST_LOOPING_VOICES;
            else if (strcmp(q, "unclamped") == 0) quirks = UFMOD_QUIRK_UNCLAMPED_GLOBAL_VOLSLIDE;
        } else if (strcmp(argv[i], "--skidrow") == 0) {
            quirks = UFMOD_QUIRK_SKIDROW_LAUNCHER;
        } else if (argv[i][0] != '-') {
            duration = atof(argv[i]);
        }
    }
    if (duration == 0.0 && target_loops == 0) {
        duration = 60.0;
    }

    FILE *f_in = fopen(in_path, "rb");
    if (!f_in) {
        fprintf(stderr, "Error opening input file: %s\n", in_path);
        return 1;
    }

    fseek(f_in, 0, SEEK_END);
    long file_size = ftell(f_in);
    fseek(f_in, 0, SEEK_SET);

    uint8_t *xm_data = (uint8_t*)malloc(file_size);
    if (!xm_data || fread(xm_data, 1, file_size, f_in) != (size_t)file_size) {
        fprintf(stderr, "Failed to read input file into memory\n");
        fclose(f_in);
        free(xm_data);
        return 1;
    }
    fclose(f_in);

    const uint32_t sample_rate = 48000;
    ufmod_t *ctx = ufmod_load(xm_data, file_size, sample_rate);
    free(xm_data);

    if (!ctx) {
        fprintf(stderr, "Failed to parse XM module with uFMOD\n");
        return 1;
    }

    if (quirks != 0xFFFFFFFF) {
        ufmod_set_quirks(ctx, quirks);
    }

    if (target_loops > 0) {
        ufmod_set_target_loops(ctx, target_loops);
        printf("Rendering at %u Hz, %d loops...\n", sample_rate, target_loops);
    } else {
        printf("Rendering at %u Hz, duration %.1f s...\n", sample_rate, duration);
    }

    FILE *f_out = fopen(out_path, "wb");
    if (!f_out) {
        fprintf(stderr, "Error opening output file: %s\n", out_path);
        ufmod_free(ctx);
        return 1;
    }

    write_wav_header(f_out, sample_rate, 0);

    const size_t chunk_frames = 1024;
    int16_t pcm_buf[chunk_frames * 2];
    size_t total_frames_target = (target_loops > 0) ? (size_t)-1 : (size_t)(duration * sample_rate);
    size_t total_rendered = 0;

    while (total_rendered < total_frames_target) {
        size_t to_render = chunk_frames;
        if (total_rendered + to_render > total_frames_target) {
            to_render = total_frames_target - total_rendered;
        }

        size_t got = ufmod_render(ctx, pcm_buf, to_render);
        if (got == 0) break;

        fwrite(pcm_buf, sizeof(int16_t) * 2, got, f_out);
        total_rendered += got;
    }

    write_wav_header(f_out, sample_rate, (uint32_t)total_rendered);
    fclose(f_out);
    ufmod_free(ctx);

    printf("Successfully wrote %zu frames (%.2f s) to %s\n",
           total_rendered, (double)total_rendered / sample_rate, out_path);

    return 0;
}
