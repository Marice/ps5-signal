/* ps5-signal: a background payload that plays beep and LED sequences for homebrew, through a small HTTP API on
 * 127.0.0.1:9150 (src/http.c), and a startup sequence from /data/ps5-signal/config.json. */
#include "config.h"
#include "http.h"
#include "icc.h"
#include "indicator.h"
#include "log.h"
#include "player.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Starts the startup sequences and holds the LED colour config.json asks for. A config that isn't valid is left
 * as it is and nothing plays: the log says what is wrong. */
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
}

static void on_signal(int number) {
    (void)number;
    player_stop();
    usleep(100 * 1000); /* the LED player puts the LED back */
    _exit(0);
}

int main(void) {
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);
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
    http_serve(server);
    return 0;
}
