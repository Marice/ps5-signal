/* Host tests: the ICC and the LED device replaced by fakes that log what the service asks for. Beeps and LED
 * steps take no time on the host, the player's waits do. */
#include "../src/icc.h"
#include "../src/indicator.h"

#include <stdio.h>
#include <string.h>

static int led_level = 1;
static int channels[3] = { 0, 255, 0 };

void icc_init(void) {}
bool icc_beep_available(void) { return true; }
bool icc_led_available(void) { return true; }
int icc_beep(int tone) { fprintf(stderr, "[fake] beep %d\n", tone); return 0; }
int icc_set_led(int level) { fprintf(stderr, "[fake] led level %d\n", level); led_level = level; return 0; }
int icc_led_level(void) { return led_level; }
int icc_beeper_muted(void) { return 0; }
static int beeper_volume = 1;
int icc_beeper_volume(void) { return beeper_volume; }
int icc_set_beeper_volume(int volume) {
    fprintf(stderr, "[fake] beeper volume %d\n", volume);
    beeper_volume = volume;
    return 0;
}

void indicator_init(void) {}
bool indicator_available(void) { return true; }
bool indicator_read(uint8_t state[INDICATOR_STATE_SIZE]) {
    memset(state, 0, INDICATOR_STATE_SIZE);
    memcpy(state, channels, sizeof(channels));
    return true;
}
bool indicator_write(const uint8_t state[INDICATOR_STATE_SIZE]) {
    memcpy(channels, state, sizeof(channels));
    fprintf(stderr, "[fake] led state restored %d/%d/%d\n", channels[0], channels[1], channels[2]);
    return true;
}
bool indicator_enable(bool on) { fprintf(stderr, "[fake] led %s\n", on ? "on" : "off"); return true; }
bool indicator_set_color(int blue, int white, int amber) {
    fprintf(stderr, "[fake] led colour %d/%d/%d\n", blue, white, amber);
    channels[0] = blue; channels[1] = white; channels[2] = amber;
    return true;
}
bool indicator_shows(int blue, int white, int amber) {
    return channels[0] == blue && channels[1] == white && channels[2] == amber;
}
void indicator_levels(int *blue, int *white, int *amber) { *blue = channels[0]; *white = channels[1]; *amber = channels[2]; }

/* The sensors: FAKE_TEMPERATURE in the environment, 50 C otherwise. */
#include <stdlib.h>
int sceKernelGetCpuTemperature(int *celsius) {
    const char *value = getenv("FAKE_TEMPERATURE");
    *celsius = value != NULL ? atoi(value) : 50;
    return 0;
}
int sceKernelGetSocSensorTemperature(int sensor, int *celsius) {
    (void)sensor;
    return sceKernelGetCpuTemperature(celsius);
}
