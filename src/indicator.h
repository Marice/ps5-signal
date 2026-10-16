/* The console LED's colour channels (blue, white, amber) through /dev/icc_indicator. Facts from PS5-Suite
 * (github.com/manos555555/PS5-Suite, payload/main.c): the ioctls 0x40829505 (read the 130-byte LED state) and
 * 0x80829504 (write it), 0x8001950e (LED on/off), and the state's 6-byte channel records
 * { channel, 1, ?, level, ?, mode } with channels 0x01 blue, 0x11 white, 0x21 amber. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define INDICATOR_STATE_SIZE 130

/* Reads the LED state and checks it holds the three channel records; false leaves colour unavailable. */
void indicator_init(void);
bool indicator_available(void);

/* The whole state, to put it back after a pattern. */
bool indicator_read(uint8_t state[INDICATOR_STATE_SIZE]);
bool indicator_write(const uint8_t state[INDICATOR_STATE_SIZE]);
/* Turns the LED on (1, the system's normal state) or off (0). */
bool indicator_enable(bool on);

/* Sets the channel levels (0-255) from the current state; all three 0 turns the LED off instead (the ICC
 * refuses an all-zero state). */
bool indicator_set_color(int blue, int white, int amber);
/* Whether the LED shows these levels now (the system may take the LED back). */
bool indicator_shows(int blue, int white, int amber);
/* The current levels, or -1 each when they can't be read. */
void indicator_levels(int *blue, int *white, int *amber);
