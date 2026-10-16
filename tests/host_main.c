/* Host tests: the service's HTTP API and player without the console (tests/run-host-tests.sh). */
#include "../src/http.h"
#include "../src/player.h"

#include <stdio.h>

int main(void) {
    int server = http_listen(SIGNAL_PORT, false);
    if (server < 0) {
        fprintf(stderr, "port %d in use\n", SIGNAL_PORT);
        return 1;
    }
    player_start();
    http_serve(server);
    return 0;
}
