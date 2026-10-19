/* ps5-signal: a background payload that plays beep and LED sequences for homebrew, through a small HTTP API on
 * 127.0.0.1:9150 (src/http.c), and a startup sequence from /data/ps5-signal/config.json. */
#include "config.h"
#include "http.h"
#include "icc.h"
#include "indicator.h"
#include "log.h"
#include "player.h"
#include "version.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

/* Starts the startup sequences and holds the LED colour config.json asks for. A config that isn't valid is left
 * as it is and nothing plays: the log says what is wrong. */
/* The version marker stays in the file even though nothing reads it here (see version.h). */
__attribute__((used)) static const volatile char version_marker[] = SIGNAL_VERSION_MARKER;

static void apply_config(void) {
    static config settings;
    char error[256];
    if (!config_load(&settings, error, sizeof(error))) {
        log_printf("%s: %s: no startup sequence\n", CONFIG_PATH, error);
        return;
    }
    if (settings.has_color) player_hold_color(true, settings.blue, settings.white, settings.amber);
    if (settings.beep_count > 0) player_beep(settings.beeps, settings.beep_count);
    if (settings.led_count > 0) player_led(settings.leds, settings.led_count, 1);
    /* After the startup light: it shows first, then the temperature. */
    if (settings.temperature) player_temperature(true, settings.cool, settings.warm);
}

#if defined(SIGNAL_DEV)
/* Development build: every change of the LED state, with the time and clock jumps, written to led-trace.log, to
 * learn the system's programs (around rest mode, for one). Never call sceKernelIsMainOnStandbyMode() here: with it
 * polled, the process was killed when the console slept. */
#include <pthread.h>
#include <string.h>
#include <time.h>

static void *trace_led(void *unused) {
    (void)unused;
    FILE *trace = fopen(CONFIG_DIRECTORY "/led-trace.log", "a");
    if (trace == NULL) return NULL;
    uint8_t last[INDICATOR_STATE_SIZE] = { 0 };
    struct timespec previous_real, previous_mono;
    clock_gettime(CLOCK_REALTIME, &previous_real);
    clock_gettime(CLOCK_MONOTONIC, &previous_mono);
    fprintf(trace, "--- trace started\n");
    fflush(trace);
    for (;;) {
        usleep(250 * 1000);
        struct timespec real, mono;
        clock_gettime(CLOCK_REALTIME, &real);
        clock_gettime(CLOCK_MONOTONIC, &mono);
        double real_gap = (real.tv_sec - previous_real.tv_sec) + (real.tv_nsec - previous_real.tv_nsec) / 1e9;
        double mono_gap = (mono.tv_sec - previous_mono.tv_sec) + (mono.tv_nsec - previous_mono.tv_nsec) / 1e9;
        previous_real = real;
        previous_mono = mono;
        struct tm local;
        localtime_r(&real.tv_sec, &local);
        char when[32];
        snprintf(when, sizeof(when), "%02d:%02d:%02d.%03ld", local.tm_hour, local.tm_min, local.tm_sec, real.tv_nsec / 1000000);
        if (real_gap > 2.0 || mono_gap > 2.0)
            fprintf(trace, "%s clock gap: real %.1f s, monotonic %.1f s\n", when, real_gap, mono_gap);
        uint8_t state[INDICATOR_STATE_SIZE];
        if (indicator_read(state) && memcmp(state, last, sizeof(state)) != 0) {
            memcpy(last, state, sizeof(state));
            fprintf(trace, "%s state", when);
            /* Header, then each 42-byte channel block up to its last non-zero byte. */
            fprintf(trace, " %02x%02x%02x%02x", state[0], state[1], state[2], state[3]);
            for (int block = 4; block + 42 <= INDICATOR_STATE_SIZE; block += 42) {
                int end = block + 42;
                while (end > block && state[end - 1] == 0) end--;
                fprintf(trace, " |");
                for (int i = block; i < end; i++) fprintf(trace, " %02x", state[i]);
            }
            fprintf(trace, "\n");
        }
        fflush(trace);
    }
    return NULL;
}
#endif

/* SIGCONT on waking from rest mode: ps5-payload-manager reopens its socket on it. Not seen here yet; the clock jump
 * in http_serve covers a wake without it. */
static void on_resume(int number) {
    (void)number;
    http_reopen();
}

static void on_signal(int number) {
    (void)number;
    player_stop();
    usleep(100 * 1000); /* the LED player puts the LED back */
    _exit(0);
}

/* In libkernel. The ELF loader names a payload sent to it "payload.elf"; this name is what process lists
 * (pldmgr, PayLoader) show instead. At most 19 characters, ending in ".elf" so they count it as a payload.
 * (A process name, not a thread name: naming threads with pthread_setname_np hangs on the console.) */
int sceKernelSetProcessName(const char *name);

int main(void) {
    sceKernelSetProcessName("ps5-signal.elf");
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCONT, on_resume);
#if defined(SIGNAL_DEV)
    bool any_address = true;
#else
    bool any_address = false;
#endif
    /* The port first: a second instance stops here, before it beeps or touches the ICC. */
    int server = http_listen(SIGNAL_PORT, any_address);
    if (server < 0) {
        log_printf("port %d is in use: ps5-signal is already running\n", SIGNAL_PORT);
        return 1;
    }
    icc_init();
    indicator_init();
    player_start();
    apply_config();
#if defined(SIGNAL_DEV)
    pthread_t tracer;
    pthread_create(&tracer, NULL, trace_led, NULL);
    pthread_detach(tracer);
#endif
    http_serve(server, SIGNAL_PORT, any_address);
    return 0;
}
