#include <sndfile.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    const int formats[] = {SF_FORMAT_WAV | SF_FORMAT_PCM_16,
                           SF_FORMAT_FLAC | SF_FORMAT_PCM_16};
    const char *names[] = {"WAV", "FLAC"};
    float samples[256], decoded[256];
    for (int i = 0; i < 256; i++) samples[i] = (i - 128) / 256.0f;
    for (unsigned int kind = 0; kind < sizeof(formats) / sizeof(formats[0]); kind++) {
        SF_INFO info = {0};
        info.samplerate = 16000;
        info.channels = 1;
        info.format = formats[kind];
        if (!sf_format_check(&info)) {
            fprintf(stderr, "libsndfile required format unavailable: %s\n", names[kind]);
            return 1;
        }
        FILE *temporary = tmpfile();
        if (!temporary) return 2;
        /* Duplicate the descriptor: sf_close owns its own descriptor only. */
        SNDFILE *writer = sf_open_fd(dup(fileno(temporary)), SFM_WRITE, &info, SF_TRUE);
        if (!writer) {
            fprintf(stderr, "libsndfile %s writer unavailable: %s\n", names[kind], sf_strerror(NULL));
            return 3;
        }
        if (sf_writef_float(writer, samples, 256) != 256) {
            fprintf(stderr, "libsndfile %s write failed: %s\n", names[kind], sf_strerror(writer));
            return 3;
        }
        if (sf_close(writer) != 0 || fseek(temporary, 0, SEEK_SET) != 0) return 4;
        memset(&info, 0, sizeof(info));
        SNDFILE *reader = sf_open_fd(dup(fileno(temporary)), SFM_READ, &info, SF_TRUE);
        if (!reader || info.samplerate != 16000 || info.channels != 1 || info.frames != 256) return 5;
        if (sf_readf_float(reader, decoded, 256) != 256 || sf_close(reader) != 0) return 6;
        fclose(temporary);
        for (int i = 0; i < 256; i++) if (fabsf(samples[i] - decoded[i]) > 1.0f / 32768.0f) return 7;
        printf("libsndfile %s PCM roundtrip: PASS\n", names[kind]);
    }
    SF_INFO info = {0};
    info.samplerate = 48000;
    info.channels = 2;
    info.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
    if (!sf_format_check(&info)) { fputs("libsndfile Vorbis unavailable\n", stderr); return 8; }
    info.format = SF_FORMAT_OGG | SF_FORMAT_OPUS;
    if (!sf_format_check(&info)) { fputs("libsndfile Opus unavailable\n", stderr); return 9; }
    puts("libsndfile WAV/FLAC roundtrips and Vorbis/Opus capability: PASS");
    return 0;
}
