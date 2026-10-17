#include "player.h"
#include "icc.h"
#include "indicator.h"
#include "log.h"
#include "temperature.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* One kind of sequence: its thread waits for a job, plays it, and refuses others meanwhile. */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t wake;
    bool pending;
    atomic_bool playing, cancel;
    int count, repeat;
    beep_step beeps[PLAYER_MAX_CONFIG_BEEPS];
    led_step leds[PLAYER_MAX_LED_STEPS];
} channel;

static channel beeper = { .lock = PTHREAD_MUTEX_INITIALIZER, .wake = PTHREAD_COND_INITIALIZER };
static channel light = { .lock = PTHREAD_MUTEX_INITIALIZER, .wake = PTHREAD_COND_INITIALIZER };

/* Per-minute counters. */
static pthread_mutex_t limits = PTHREAD_MUTEX_INITIALIZER;
static time_t window_start;
static int led_changes, beep_requests;

static void roll_window(void) {
    time_t now = time(NULL);
    if (now - window_start >= 60) {
        window_start = now;
        led_changes = 0;
        beep_requests = 0;
    }
}

/* The colour held between sequences, and the system's LED state from before the service set any. */
static pthread_mutex_t held_lock = PTHREAD_MUTEX_INITIALIZER;
static bool held;
static int held_blue, held_white, held_amber;
static uint8_t system_state[INDICATOR_STATE_SIZE];
static bool have_system_state;

/* Shows the held colour if there is one; false when there is none. A held "off" is set after a sequence only
 * (once off, the channel levels still read as they were, so a check would set it again every time). */
static bool restore_held_color(bool after_sequence) {
    pthread_mutex_lock(&held_lock);
    bool hold = held;
    int b = held_blue, w = held_white, a = held_amber;
    pthread_mutex_unlock(&held_lock);
    bool off = b == 0 && w == 0 && a == 0;
    if (hold && (off ? after_sequence : !indicator_shows(b, w, a))) indicator_set_color(b, w, a);
    return hold;
}

/* Sleeps in short slices so a stop is noticed quickly; false when stopped. */
static bool wait_ms(channel *c, int ms) {
    while (ms > 0) {
        if (atomic_load(&c->cancel)) return false;
        int slice = ms < 20 ? ms : 20;
        usleep((useconds_t)slice * 1000);
        ms -= slice;
    }
    return !atomic_load(&c->cancel);
}

static void play_beeps(channel *c) {
    for (int i = 0; i < c->count; i++) {
        int result = icc_beep(c->beeps[i].tone);
        if (result != 0) log_printf("beep %d failed: 0x%x\n", c->beeps[i].tone, result);
        if (i + 1 < c->count && !wait_ms(c, c->beeps[i].pause_ms)) return;
    }
}

static void play_leds(channel *c) {
    /* Put the player's own brightness and the system's LED state back afterwards, also when stopped. */
    bool colour = false;
    for (int i = 0; i < c->count; i++) colour = colour || c->leds[i].level < 0;
    int before_level = icc_led_level();
    uint8_t before_state[INDICATOR_STATE_SIZE];
    bool have_state = colour && indicator_read(before_state);
    for (int r = 0; r < c->repeat; r++) {
        for (int i = 0; i < c->count; i++) {
            const led_step *step = &c->leds[i];
            if (step->level >= 0) {
                int result = icc_set_led(step->level);
                if (result != 0) log_printf("led %d failed: 0x%x\n", step->level, result);
                if (!wait_ms(c, step->ms)) goto restore;
                continue;
            }
            if (!indicator_set_color(step->blue, step->white, step->amber)) {
                log_printf("led colour %d/%d/%d failed\n", step->blue, step->white, step->amber);
            }
            /* The system may take the LED back during a step: check twice a second and set it again. */
            bool off = step->blue == 0 && step->white == 0 && step->amber == 0;
            for (int left = step->ms; left > 0; left -= 500) {
                if (!wait_ms(c, left < 500 ? left : 500)) goto restore;
                if (!off && left > 500 && !indicator_shows(step->blue, step->white, step->amber)) {
                    indicator_set_color(step->blue, step->white, step->amber);
                }
            }
        }
    }
restore:
    if (before_level >= 0) icc_set_led(before_level);
    if (!restore_held_color(true) && have_state) {
        indicator_write(before_state);
    }
}

static void *run(void *argument) {
    channel *c = argument;
    for (;;) {
        pthread_mutex_lock(&c->lock);
        while (!c->pending) pthread_cond_wait(&c->wake, &c->lock);
        c->pending = false;
        pthread_mutex_unlock(&c->lock);
        if (c == &beeper) play_beeps(c);
        else play_leds(c);
        atomic_store(&c->playing, false);
    }
    return NULL;
}

static bool temperature_shown(void);

/* Keeps the held colour while no LED sequence plays: the system may take the LED back. */
static void *keep_held_color(void *unused) {
    (void)unused;
    for (;;) {
        sleep(2);
        if (!atomic_load(&light.playing) && !temperature_shown()) restore_held_color(false);
    }
    return NULL;
}

/* The temperature on the LED: the colour for the hottest sensor, set when it changes. The held colour (the
 * player's own) is kept aside and comes back when this stops. */
#define TEMPERATURE_PERIOD_SECONDS 3
static pthread_mutex_t temperature_lock = PTHREAD_MUTEX_INITIALIZER;
static bool temperature_on;
static int temperature_cool = TEMPERATURE_COOL, temperature_warm = TEMPERATURE_WARM;
static int shown_blue = -1, shown_white = -1, shown_amber = -1;

static void show_temperature(void) {
    pthread_mutex_lock(&temperature_lock);
    bool on = temperature_on;
    int cool = temperature_cool, warm = temperature_warm;
    pthread_mutex_unlock(&temperature_lock);
    if (!on || atomic_load(&light.playing)) return;
    int celsius = temperature_now();
    if (celsius < 0) return;
    int b, w, a;
    temperature_color(celsius, cool, warm, &b, &w, &a);
    /* Set again when the system took the LED back, as a held colour is. */
    if (b == shown_blue && w == shown_white && a == shown_amber && indicator_shows(b, w, a)) return;
    if (indicator_set_color(b, w, a)) {
        shown_blue = b;
        shown_white = w;
        shown_amber = a;
    }
}

static void *watch_temperature(void *unused) {
    (void)unused;
    for (;;) {
        show_temperature();
        sleep(TEMPERATURE_PERIOD_SECONDS);
    }
    return NULL;
}

static bool temperature_shown(void) {
    pthread_mutex_lock(&temperature_lock);
    bool on = temperature_on;
    pthread_mutex_unlock(&temperature_lock);
    return on;
}

void player_temperature(bool on, int cool, int warm) {
    pthread_mutex_lock(&temperature_lock);
    bool was = temperature_on;
    temperature_on = on;
    temperature_cool = cool;
    temperature_warm = warm;
    shown_blue = shown_white = shown_amber = -1;
    pthread_mutex_unlock(&temperature_lock);
    if (on) {
        show_temperature();
    } else if (was) {
        /* The player's own colour, or the system's, again. */
        if (!restore_held_color(true) && have_system_state) {
            indicator_write(system_state);
        }
    }
}

bool player_temperature_shown(int *cool, int *warm) {
    pthread_mutex_lock(&temperature_lock);
    bool on = temperature_on;
    *cool = temperature_cool;
    *warm = temperature_warm;
    pthread_mutex_unlock(&temperature_lock);
    return on;
}

void player_start(void) {
    window_start = time(NULL);
    have_system_state = indicator_available() && indicator_read(system_state);
    pthread_t thread;
    pthread_create(&thread, NULL, keep_held_color, NULL);
    pthread_detach(thread);
    pthread_create(&thread, NULL, watch_temperature, NULL);
    pthread_detach(thread);
    pthread_create(&thread, NULL, run, &beeper);
    pthread_detach(thread);
    pthread_create(&thread, NULL, run, &light);
    pthread_detach(thread);
}

/* Claims the channel for a new sequence; false while one plays. */
static bool claim(channel *c) {
    bool idle = false;
    return atomic_compare_exchange_strong(&c->playing, &idle, true);
}

static void begin(channel *c) {
    pthread_mutex_lock(&c->lock);
    atomic_store(&c->cancel, false);
    c->pending = true;
    pthread_cond_signal(&c->wake);
    pthread_mutex_unlock(&c->lock);
}

player_result player_beep(const beep_step *steps, int count) {
    pthread_mutex_lock(&limits);
    roll_window();
    bool allowed = beep_requests < PLAYER_BEEP_REQUESTS_PER_MINUTE;
    pthread_mutex_unlock(&limits);
    if (!allowed) return PLAYER_LIMITED;
    if (!claim(&beeper)) return PLAYER_BUSY;
    pthread_mutex_lock(&limits);
    beep_requests++;
    pthread_mutex_unlock(&limits);
    memcpy(beeper.beeps, steps, sizeof(*steps) * (size_t)count);
    beeper.count = count;
    begin(&beeper);
    return PLAYER_STARTED;
}

player_result player_led(const led_step *steps, int count, int repeat) {
    if (temperature_shown()) return PLAYER_TEMPERATURE;
    for (int i = 0; i < count; i++) {
        if (steps[i].level >= 0 ? !icc_led_available() : !indicator_available()) return PLAYER_UNAVAILABLE;
    }
    int changes = count * repeat + 1; /* the steps and putting the LED back */
    pthread_mutex_lock(&limits);
    roll_window();
    bool allowed = led_changes + changes <= PLAYER_LED_CHANGES_PER_MINUTE;
    pthread_mutex_unlock(&limits);
    if (!allowed) return PLAYER_LIMITED;
    if (!claim(&light)) return PLAYER_BUSY;
    pthread_mutex_lock(&limits);
    led_changes += changes;
    pthread_mutex_unlock(&limits);
    memcpy(light.leds, steps, sizeof(*steps) * (size_t)count);
    light.count = count;
    light.repeat = repeat;
    begin(&light);
    return PLAYER_STARTED;
}

void player_stop(void) {
    atomic_store(&beeper.cancel, true);
    atomic_store(&light.cancel, true);
}

bool player_beeping(void) { return atomic_load(&beeper.playing); }
bool player_lighting(void) { return atomic_load(&light.playing); }

player_result player_hold_color(bool hold, int blue, int white, int amber) {
    if (!indicator_available()) return PLAYER_UNAVAILABLE;
    if (temperature_shown()) {
        /* Kept aside: it shows again when the temperature stops showing. */
        pthread_mutex_lock(&held_lock);
        held = hold;
        held_blue = blue;
        held_white = white;
        held_amber = amber;
        pthread_mutex_unlock(&held_lock);
        return PLAYER_TEMPERATURE;
    }
    pthread_mutex_lock(&limits);
    roll_window();
    bool allowed = led_changes + 1 <= PLAYER_LED_CHANGES_PER_MINUTE;
    if (allowed) led_changes++;
    pthread_mutex_unlock(&limits);
    if (!allowed) return PLAYER_LIMITED;
    pthread_mutex_lock(&held_lock);
    held = hold;
    held_blue = blue;
    held_white = white;
    held_amber = amber;
    pthread_mutex_unlock(&held_lock);
    if (atomic_load(&light.playing)) return PLAYER_STARTED; /* shown when the sequence ends */
    if (hold) {
        indicator_set_color(blue, white, amber);
    } else if (have_system_state) {
        indicator_write(system_state);
    }
    return PLAYER_STARTED;
}

bool player_held_color(int *blue, int *white, int *amber) {
    pthread_mutex_lock(&held_lock);
    bool hold = held;
    *blue = held_blue;
    *white = held_white;
    *amber = held_amber;
    pthread_mutex_unlock(&held_lock);
    return hold;
}

