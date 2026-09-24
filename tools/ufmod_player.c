#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#include "ufmod.h"

static struct termios orig_termios;
static int termios_saved = 0;

static volatile int g_running = 1;
static volatile int g_paused = 0;
static volatile int g_infinite_loop = 1;
static pthread_mutex_t g_audio_mutex = PTHREAD_MUTEX_INITIALIZER;

static void reset_terminal(void) {
    if (termios_saved) {
        printf("\033[?25h\033[0m\n");
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        termios_saved = 0;
    }
}

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void setup_terminal(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
        termios_saved = 1;
        atexit(reset_terminal);
        struct termios raw = orig_termios;
        raw.c_lflag &= ~(ECHO | ICANON);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        printf("\033[2J\033[?25l");
    }
}

typedef struct {
    ufmod_t        *ctx;
    snd_pcm_t      *pcm;
    unsigned int    sample_rate;
} audio_thread_args_t;

static void* audio_thread_func(void *arg) {
    audio_thread_args_t *args = (audio_thread_args_t*)arg;
    const size_t chunk_size = 1024;
    int16_t buffer[chunk_size * 2];

    while (g_running) {
        if (g_paused) {
            usleep(20000);
            continue;
        }

        pthread_mutex_lock(&g_audio_mutex);
        size_t rendered = ufmod_render(args->ctx, buffer, chunk_size);
        pthread_mutex_unlock(&g_audio_mutex);

        if (rendered == 0) {
            if (!g_infinite_loop) {
                g_running = 0;
                break;
            }
            usleep(10000);
            continue;
        }

        snd_pcm_sframes_t frames = snd_pcm_writei(args->pcm, buffer, rendered);
        if (frames < 0) {
            frames = snd_pcm_recover(args->pcm, (int)frames, 1);
            if (frames < 0) {
                usleep(5000);
            }
        }
    }

    return NULL;
}

static void draw_meter(char *out, size_t out_len, int vol, int max_vol, int width) {
    if (vol < 0) vol = 0;
    if (vol > max_vol) vol = max_vol;
    int filled = (vol * width) / max_vol;
    if (filled > width) filled = width;

    size_t pos = 0;
    out[pos++] = '[';
    for (int i = 0; i < width && pos + 2 < out_len; i++) {
        out[pos++] = (i < filled) ? '#' : ' ';
    }
    out[pos++] = ']';
    out[pos] = '\0';
}

static void render_ui(ufmod_t *ctx, const char *filename, unsigned int num_channels, unsigned int num_orders) {
    unsigned int row = 0, order = 0;
    unsigned int time_ms = ufmod_get_time(ctx);
    unsigned int sec = time_ms / 1000;
    unsigned int min = sec / 60;
    sec %= 60;

    ufmod_get_row_order(ctx, &row, &order);
    int loop_count = ufmod_get_loop_count(ctx);
    unsigned int vol = ufmod_get_volume(ctx);
    int vol_pct = (int)((vol * 100) / UFMOD_VOL_MAX);

    char vol_bar[32];
    draw_meter(vol_bar, sizeof(vol_bar), (int)vol, UFMOD_VOL_MAX, 16);

    printf("\033[H");
    printf("\033[1;37m uFMOD Player\033[0m\n\n");

    const char *title = ufmod_get_title(ctx);
    printf(" File   : \033[1;32m%-40s\033[0m\n", filename);
    printf(" Title  : \033[1;33m%-40s\033[0m\n", (title && title[0]) ? title : "(Untitled)");
    printf(" Status : %s   \033[0mTime : \033[1;37m%02u:%02u\033[0m   Loop : \033[1;35m%d\033[0m (%s)\n",
           g_paused ? "\033[1;33m[PAUSED] " : "\033[1;32m[PLAYING]",
           min, sec, loop_count,
           g_infinite_loop ? "\033[1;32mInfinite: ON\033[0m" : "\033[1;31mInfinite: OFF\033[0m");

    printf(" Pos    : Order \033[1;37m%02u/%02u\033[0m   Row \033[1;37m%02u/64\033[0m   Channels : \033[1;37m%u\033[0m\n",
           order, num_orders > 0 ? num_orders - 1 : 0, row, num_channels);

    printf(" Volume : %s \033[1;37m%3d%%\033[0m\n", vol_bar, vol_pct);
    printf("\033[1;36m----------------------------------------------------------------------\033[0m\n");

    for (unsigned int ch = 0; ch < num_channels; ch += 2) {
        int v0 = ufmod_get_channel_volume(ctx, ch);
        char bar0[24];
        draw_meter(bar0, sizeof(bar0), v0, 127, 10);

        if (ch + 1 < num_channels) {
            int v1 = ufmod_get_channel_volume(ctx, ch + 1);
            char bar1[24];
            draw_meter(bar1, sizeof(bar1), v1, 127, 10);
            printf(" Ch%02u: %s %3d  |  Ch%02u: %s %3d\n",
                   ch + 1, bar0, v0, ch + 2, bar1, v1);
        } else {
            printf(" Ch%02u: %s %3d\n", ch + 1, bar0, v0);
        }
    }

    printf("\033[1;36m----------------------------------------------------------------------\033[0m\n");
    printf("\033[1;37m Controls:\033[0m\n");
    printf("   [Space] Play/Pause       [+/-] Volume        [Left/Right] Order\n");
    printf("   [r]     Restart Track    [l]   Toggle Loop   [q/ESC]      Quit\n");
    printf("\033[1;36m======================================================================\033[0m\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.xm> [--noloop] [--rate <hz>] [--vol <0-256>]\n", argv[0]);
        return 1;
    }

    const char *in_path = argv[1];
    unsigned int sample_rate = 48000;
    int initial_vol = 192;
    int infinite_loop = 1;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--noloop") == 0) {
            infinite_loop = 0;
        } else if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            sample_rate = (unsigned int)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--vol") == 0 && i + 1 < argc) {
            initial_vol = atoi(argv[++i]);
        }
    }

    g_infinite_loop = infinite_loop;

    FILE *f_in = fopen(in_path, "rb");
    if (!f_in) {
        perror("Error opening input file");
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

    ufmod_t *ctx = ufmod_load(xm_data, (size_t)file_size, sample_rate);
    free(xm_data);

    if (!ctx) {
        fprintf(stderr, "Failed to load XM module with uFMOD\n");
        return 1;
    }

    ufmod_set_volume(ctx, initial_vol);
    if (!g_infinite_loop) {
        ufmod_set_noloop(ctx, 1);
    }

    unsigned int num_channels = 0, num_orders = 0;
    ufmod_get_info(ctx, &num_channels, &num_orders, NULL, NULL);

    snd_pcm_t *pcm = NULL;
    int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "Failed to open ALSA PCM device: %s\n", snd_strerror(err));
        ufmod_free(ctx);
        return 1;
    }

    err = snd_pcm_set_params(pcm,
                             SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             2,
                             sample_rate,
                             1,
                             100000);
    if (err < 0) {
        fprintf(stderr, "Failed to configure ALSA PCM device: %s\n", snd_strerror(err));
        snd_pcm_close(pcm);
        ufmod_free(ctx);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    setup_terminal();

    audio_thread_args_t thread_args = {
        .ctx = ctx,
        .pcm = pcm,
        .sample_rate = sample_rate
    };

    pthread_t audio_thread;
    if (pthread_create(&audio_thread, NULL, audio_thread_func, &thread_args) != 0) {
        reset_terminal();
        fprintf(stderr, "Failed to start audio playback thread\n");
        snd_pcm_close(pcm);
        ufmod_free(ctx);
        return 1;
    }

    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };

    while (g_running) {
        render_ui(ctx, in_path, num_channels, num_orders);

        int ret = poll(&pfd, 1, 40);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            char ch = 0;
            if (read(STDIN_FILENO, &ch, 1) > 0) {
                if (ch == 'q' || ch == 'Q') {
                    g_running = 0;
                } else if (ch == 0x1B) {
                    char seq[2];
                    if (read(STDIN_FILENO, &seq[0], 1) > 0 && read(STDIN_FILENO, &seq[1], 1) > 0) {
                        if (seq[0] == '[') {
                            if (seq[1] == 'A') { // Up
                                pthread_mutex_lock(&g_audio_mutex);
                                unsigned int v = ufmod_get_volume(ctx) + 16;
                                if (v > UFMOD_VOL_MAX) v = UFMOD_VOL_MAX;
                                ufmod_set_volume(ctx, v);
                                pthread_mutex_unlock(&g_audio_mutex);
                            } else if (seq[1] == 'B') { // Down
                                pthread_mutex_lock(&g_audio_mutex);
                                unsigned int v = ufmod_get_volume(ctx);
                                v = (v > 16) ? (v - 16) : 0;
                                ufmod_set_volume(ctx, v);
                                pthread_mutex_unlock(&g_audio_mutex);
                            } else if (seq[1] == 'C') { // Right
                                unsigned int cur_ord = 0;
                                ufmod_get_row_order(ctx, NULL, &cur_ord);
                                pthread_mutex_lock(&g_audio_mutex);
                                ufmod_jump_order(ctx, (int)cur_ord + 1);
                                pthread_mutex_unlock(&g_audio_mutex);
                            } else if (seq[1] == 'D') { // Left
                                unsigned int cur_ord = 0;
                                ufmod_get_row_order(ctx, NULL, &cur_ord);
                                pthread_mutex_lock(&g_audio_mutex);
                                ufmod_jump_order(ctx, (int)cur_ord - 1);
                                pthread_mutex_unlock(&g_audio_mutex);
                            }
                        }
                    } else {
                        g_running = 0;
                    }
                } else if (ch == ' ') {
                    g_paused = !g_paused;
                } else if (ch == '+' || ch == '=') {
                    pthread_mutex_lock(&g_audio_mutex);
                    unsigned int v = ufmod_get_volume(ctx) + 16;
                    if (v > UFMOD_VOL_MAX) v = UFMOD_VOL_MAX;
                    ufmod_set_volume(ctx, v);
                    pthread_mutex_unlock(&g_audio_mutex);
                } else if (ch == '-' || ch == '_') {
                    pthread_mutex_lock(&g_audio_mutex);
                    unsigned int v = ufmod_get_volume(ctx);
                    v = (v > 16) ? (v - 16) : 0;
                    ufmod_set_volume(ctx, v);
                    pthread_mutex_unlock(&g_audio_mutex);
                } else if (ch == 'r' || ch == 'R') {
                    pthread_mutex_lock(&g_audio_mutex);
                    ufmod_restart(ctx);
                    pthread_mutex_unlock(&g_audio_mutex);
                } else if (ch == 'l' || ch == 'L') {
                    g_infinite_loop = !g_infinite_loop;
                    pthread_mutex_lock(&g_audio_mutex);
                    ufmod_set_noloop(ctx, !g_infinite_loop);
                    pthread_mutex_unlock(&g_audio_mutex);
                } else if (ch == 'n' || ch == 'N') {
                    unsigned int cur_ord = 0;
                    ufmod_get_row_order(ctx, NULL, &cur_ord);
                    pthread_mutex_lock(&g_audio_mutex);
                    ufmod_jump_order(ctx, (int)cur_ord + 1);
                    pthread_mutex_unlock(&g_audio_mutex);
                } else if (ch == 'p' || ch == 'P') {
                    unsigned int cur_ord = 0;
                    ufmod_get_row_order(ctx, NULL, &cur_ord);
                    pthread_mutex_lock(&g_audio_mutex);
                    ufmod_jump_order(ctx, (int)cur_ord - 1);
                    pthread_mutex_unlock(&g_audio_mutex);
                }
            }
        }
    }

    pthread_join(audio_thread, NULL);
    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    ufmod_free(ctx);
    reset_terminal();

    return 0;
}
