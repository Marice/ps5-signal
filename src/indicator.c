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

/* A channel's record: the records sit at no fixed place in the state, so look for (channel, 1). */
static int record(const uint8_t *state, uint8_t channel) {
    for (int i = 2; i + 5 < INDICATOR_STATE_SIZE; i++) {
        if (state[i] == channel && state[i + 1] == 0x01) return i;
    }
    return -1;
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

bool indicator_enable(bool on) {
    uint8_t argument[8] = { on ? 1 : 0 };
    pthread_mutex_lock(&lock);
    bool ok = call(IOCTL_ENABLE, argument);
    pthread_mutex_unlock(&lock);
    return ok;
}

void indicator_init(void) {
    uint8_t state[INDICATOR_STATE_SIZE];
    available = indicator_read(state) && record(state, CHANNEL_BLUE) >= 0 && record(state, CHANNEL_WHITE) >= 0 &&
                record(state, CHANNEL_AMBER) >= 0;
    int blue, white, amber;
    indicator_levels(&blue, &white, &amber);
    log_printf("indicator: colour %s (blue %d, white %d, amber %d)\n", available ? "available" : "unavailable", blue,
               white, amber);
}

bool indicator_available(void) { return available; }

bool indicator_set_color(int blue, int white, int amber) {
    if (!available) return false;
    if (blue == 0 && white == 0 && amber == 0) return indicator_enable(false);
    uint8_t state[INDICATOR_STATE_SIZE];
    if (!indicator_read(state)) return false;
    const uint8_t channels[3] = { CHANNEL_BLUE, CHANNEL_WHITE, CHANNEL_AMBER };
    const int levels[3] = { blue, white, amber };
    for (int i = 0; i < 3; i++) {
        int at = record(state, channels[i]);
        if (at < 0) return false;
        state[at + 3] = (uint8_t)levels[i];
    }
    return indicator_enable(true) && indicator_write(state);
}

void indicator_levels(int *blue, int *white, int *amber) {
    uint8_t state[INDICATOR_STATE_SIZE];
    *blue = *white = *amber = -1;
    if (!indicator_read(state)) return;
    int at;
    if ((at = record(state, CHANNEL_BLUE)) >= 0) *blue = state[at + 3];
    if ((at = record(state, CHANNEL_WHITE)) >= 0) *white = state[at + 3];
    if ((at = record(state, CHANNEL_AMBER)) >= 0) *amber = state[at + 3];
}

bool indicator_shows(int blue, int white, int amber) {
    int b, w, a;
    indicator_levels(&b, &w, &a);
    return b == blue && w == white && a == amber;
}
