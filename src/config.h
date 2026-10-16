/* /data/ps5-signal/config.json: the startup sequences and the LED colour to hold.
 *
 *   {
 *     "startup": { "beep": [ beep steps, up to 16 ], "led": [ LED steps ] },
 *     "led_color": { "blue": 0-255, "white": 0-255, "amber": 0-255 }   (or null: the system's colour)
 *   }
 *
 * The steps follow patterns.h. A missing or empty part is off. */
#pragma once

#include "player.h"

#include <stdbool.h>
#include <stddef.h>

#ifndef CONFIG_DIRECTORY /* the host tests point it elsewhere */
#define CONFIG_DIRECTORY "/data/ps5-signal"
#endif
#define CONFIG_PATH CONFIG_DIRECTORY "/config.json"
#define CONFIG_MAX_SIZE 8192

typedef struct {
    beep_step beeps[PLAYER_MAX_CONFIG_BEEPS];
    int beep_count;
    led_step leds[PLAYER_MAX_LED_STEPS];
    int led_count;
    bool has_color;
    int blue, white, amber;
} config;

/* Checks text as a config; false with a reason in error when it isn't valid. */
bool config_parse(const char *text, size_t length, config *out, char *error, size_t error_size);
/* Reads config.json, or writes the default when there is none. False (with the reason) when the file exists but
 * can't be read or isn't valid: it is left as it is. */
bool config_load(config *out, char *error, size_t error_size);
/* The file's text as it is (the default when there is none); NULL when it can't be read. */
const char *config_text(size_t *length);
/* Checks text and, when valid, writes it as config.json (through a temporary file). */
bool config_save(const char *text, size_t length, config *out, char *error, size_t error_size);
