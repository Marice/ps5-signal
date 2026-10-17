#include "indicator.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define INDICATOR_DEVICE "/dev/icc_indicator"
#define IOCTL_GET_STATE 0x40829505ul
#define IOCTL_SET_STATE 0x80829504ul
#define IOCTL_ENABLE 0x8001950eul

enum { CHANNEL_BLUE = 0x01, CHANNEL_WHITE = 0x11, CHANNEL_AMBER = 0x21 };

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bool available;

static bool call(unsigned long request, void *argument) {
    int fd = open(INDICATOR_DEVICE, O_RDWR);
    if (fd < 0) return false;
    int result = ioctl(fd, request, argument);
    int error = errno;
    close(fd);
    if (result != 0) log_printf("indicator ioctl 0x%lx failed: errno %d\n", request, error);
    return result == 0;
}

/* The state: a 4-byte header (00 00, then the channel count as u16) and a 42-byte block per channel. A block
 * is a small program: { id, step count, then 5-byte steps { type, level, 3 bytes of timing } }. The system's
 * own light runs programs of several steps (a fade); a held colour is one step per channel, written in the
 * form a one-step block has on the console: { 02, level, 05, 02, 00 }. */
#define BLOCK_START 4
#define BLOCK_SIZE 42
#define STEP_SIZE 5

/* Where a channel's block starts, or -1. */
static int block(const uint8_t *state, uint8_t channel) {
    int count = state[2] | state[3] << 8;
    for (int i = 0; i < count && BLOCK_START + (i + 1) * BLOCK_SIZE <= INDICATOR_STATE_SIZE; i++) {
        int at = BLOCK_START + i * BLOCK_SIZE;
        if (state[at] == channel) return at;
    }
    return -1;
}

/* A block's level: its last step's, the level the program ends on. */
static int block_level(const uint8_t *state, int at) {
    int steps = state[at + 1];
    if (steps < 1 || 2 + steps * STEP_SIZE > BLOCK_SIZE) return -1;
    return state[at + 2 + (steps - 1) * STEP_SIZE + 1];
}

/* Rewrites a block as one step that holds the level. */
static void hold_level(uint8_t *state, int at, int level) {
    uint8_t channel = state[at];
    memset(state + at, 0, BLOCK_SIZE);
    const uint8_t step[2 + STEP_SIZE] = { channel, 1, 0x02, (uint8_t)level, 0x05, 0x02, 0x00 };
    memcpy(state + at, step, sizeof(step));
}

bool indicator_read(uint8_t state[INDICATOR_STATE_SIZE]) {
    pthread_mutex_lock(&lock);
    bool ok = call(IOCTL_GET_STATE, state);
    pthread_mutex_unlock(&lock);
    return ok;
}

bool indicator_write(const uint8_t state[INDICATOR_STATE_SIZE]) {
    uint8_t copy[INDICATOR_STATE_SIZE];
    memcpy(copy, state, sizeof(copy));
    pthread_mutex_lock(&lock);
    bool ok = call(IOCTL_SET_STATE, copy);
    pthread_mutex_unlock(&lock);
    return ok;
}

/* ioctl 0x8001950e. Not used: it is the beeper's off setting, not the LED's on/off flag PS5-Suite took it for.
 * Writing 1 turned the console's beep setting off every time a colour was set (confirmed on a console: the beep
 * setting stayed on once it was no longer called, and colours still showed). Kept for the record. */
bool indicator_enable(bool on) {
    uint8_t argument[8] = { on ? 1 : 0 };
    pthread_mutex_lock(&lock);
    bool ok = call(IOCTL_ENABLE, argument);
    pthread_mutex_unlock(&lock);
    return ok;
}

void indicator_init(void) {
    uint8_t state[INDICATOR_STATE_SIZE];
    /* Available when the state can be read; a channel without a record in it is left alone. */
    available = indicator_read(state);
    int blue, white, amber;
    indicator_levels(&blue, &white, &amber);
    log_printf("indicator: colour %s (blue %d, white %d, amber %d)\n", available ? "available" : "unavailable", blue,
               white, amber);
}

bool indicator_available(void) { return available; }

bool indicator_set_color(int blue, int white, int amber) {
    if (!available) return false;
    uint8_t state[INDICATOR_STATE_SIZE];
    if (!indicator_read(state)) return false;
    const uint8_t channels[3] = { CHANNEL_BLUE, CHANNEL_WHITE, CHANNEL_AMBER };
    const int levels[3] = { blue, white, amber };
    bool patched = false;
    for (int i = 0; i < 3; i++) {
        int at = block(state, channels[i]);
        if (at < 0) continue;
        hold_level(state, at, levels[i]);
        patched = true;
    }
    if (!patched) {
        log_printf("indicator: no channel block in the state\n");
        return false;
    }
    /* Only the state: ioctl 0x8001950e turns the beeper off (see indicator_enable). Off is a program at level 0. */
    return indicator_write(state);
}

void indicator_levels(int *blue, int *white, int *amber) {
    uint8_t state[INDICATOR_STATE_SIZE];
    *blue = *white = *amber = -1;
    if (!indicator_read(state)) return;
    int at;
    if ((at = block(state, CHANNEL_BLUE)) >= 0) *blue = block_level(state, at);
    if ((at = block(state, CHANNEL_WHITE)) >= 0) *white = block_level(state, at);
    if ((at = block(state, CHANNEL_AMBER)) >= 0) *amber = block_level(state, at);
}

bool indicator_shows(int blue, int white, int amber) {
    int b, w, a;
    indicator_levels(&b, &w, &a);
    /* A channel without a record can't differ. */
    return (b < 0 || b == blue) && (w < 0 || w == white) && (a < 0 || a == amber);
}

bool indicator_rest_program(const uint8_t state[INDICATOR_STATE_SIZE]) {
    for (int at = 4; at + 42 <= INDICATOR_STATE_SIZE; at += 42) {
        if (state[at] == 0x10 && state[at + 1] > 1) return true;
    }
    return false;
}
