/* Plays beep and LED sequences: one beep sequence and one LED sequence at a time, each on a thread of its own
 * (they can play together). A request while its kind is playing is refused, not queued. */
#pragma once

#include <stdbool.h>

enum {
    PLAYER_MAX_API_BEEPS = 24,    /* per /beep request */
    PLAYER_MAX_CONFIG_BEEPS = 24, /* the startup tune in config.json */
    PLAYER_MIN_PAUSE_MS = 100,
    PLAYER_MAX_PAUSE_MS = 2000,
    PLAYER_MAX_LED_STEPS = 24,
    /* A beep sequence (its pauses) and a LED sequence (its steps, times repeat) last 10 seconds at most. */
    PLAYER_MAX_TOTAL_MS = 10000,
    PLAYER_MAX_LED_REPEAT = 5,
    /* Each LED step may change a setting the console keeps, possibly in flash: a step lasts at least this long,
     * and LED changes are limited per minute. */
    PLAYER_MIN_LED_MS = 250,
    PLAYER_MAX_LED_MS = 10000,
    PLAYER_LED_CHANGES_PER_MINUTE = 120,
    PLAYER_BEEP_REQUESTS_PER_MINUTE = 30,
};

typedef struct { int tone; int pause_ms; } beep_step;
/* A brightness step (level 0-2, colours -1) or a colour step (level -1; blue, white and amber 0-255, all three 0
 * for off). */
typedef struct { int level; int blue, white, amber; int ms; } led_step;

typedef enum {
    PLAYER_STARTED,     /* playing now */
    PLAYER_BUSY,        /* a sequence of this kind is playing */
    PLAYER_LIMITED,     /* over the per-minute limit */
    PLAYER_UNAVAILABLE, /* not possible on this console */
    PLAYER_TEMPERATURE, /* the LED shows the temperature: LED patterns and colours are refused */
} player_result;

void player_start(void);
/* The steps are checked by the caller (patterns.c). */
player_result player_beep(const beep_step *steps, int count);
player_result player_led(const led_step *steps, int count, int repeat);
/* Ends both sequences; the LED is put back as it was. */
void player_stop(void);
/* The LED colour to hold between sequences (the system may take the LED back: it is set again when it changed),
 * or, with hold false, the system's own LED state again. */
player_result player_hold_color(bool hold, int blue, int white, int amber);
/* Shows the temperature on the LED (checked every 3 seconds), between cool and warm degrees, or stops it (the
 * held colour, or the system's, again). While on, player_led and player_hold_color refuse. */
void player_temperature(bool on, int cool, int warm);
/* Whether the temperature shows, and its range. */
bool player_temperature_shown(int *cool, int *warm);
/* The held colour; false when the system's colour shows. */
bool player_held_color(int *blue, int *white, int *amber);
bool player_beeping(void);
bool player_lighting(void);
