/* ps5_signal.h: plays beep and LED patterns through the ps5-signal service (127.0.0.1:9150) from a payload or
 * homebrew on the console. Single header: in exactly one source file, #define PS5_SIGNAL_IMPLEMENTATION before
 * including it.
 *
 *   if (ps5_signal_running()) {
 *       ps5_signal_beep("[{\"tone\":1,\"pause_ms\":200},{\"tone\":1}]");
 *       ps5_signal_led("[{\"color\":\"orange\",\"ms\":400},{\"level\":2,\"ms\":400}]", 3);
 *   }
 *   ps5_signal_color(255, 0, 60);   (hold a colour, not saved)
 *   ps5_signal_color_system();      (give the light back to the system)
 *   ps5_signal_stop();
 *
 * Each returns the HTTP status (202 playing, 400 invalid, 409 a sequence of that kind is playing, 429 over the
 * limit, 503 not available on this console), or -1 when the service doesn't answer (not running). See
 * docs/API.md for the steps and the rules.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5_SIGNAL_H
#define PS5_SIGNAL_H

/* 1 when the service answers, 0 when it doesn't. */
int ps5_signal_running(void);
/* pattern: the JSON array of steps (1-24, 10 seconds in all); repeat (LED only): 1-5. */
int ps5_signal_beep(const char *pattern);
int ps5_signal_led(const char *pattern, int repeat);
/* Holds a colour (channels 0-255) without saving it, or the system's colour again. */
int ps5_signal_color(int blue, int white, int amber);
int ps5_signal_color_system(void);
/* Ends the beep and the LED sequence that play now. */
int ps5_signal_stop(void);

#ifdef PS5_SIGNAL_IMPLEMENTATION
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int ps5_signal_request(const char *method, const char *path, const char *body) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct timeval timeout = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(9150);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    char head[256];
    int n = snprintf(head, sizeof(head),
                     "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
                     "Content-Length: %zu\r\nConnection: close\r\n\r\n", method, path, strlen(body));
    if (send(fd, head, (size_t)n, 0) != n || send(fd, body, strlen(body), 0) != (ssize_t)strlen(body)) {
        close(fd);
        return -1;
    }
    char reply[64] = {0};
    ssize_t got = recv(fd, reply, sizeof(reply) - 1, 0);
    close(fd);
    int status = -1;
    if (got > 0 && sscanf(reply, "HTTP/1.%*d %d", &status) != 1) status = -1;
    return status;
}

static int ps5_signal_pattern(const char *path, const char *pattern, int repeat) {
    size_t size = strlen(pattern) + 48;
    char *body = (char *)malloc(size);
    if (body == NULL) return -1;
    if (repeat > 0) snprintf(body, size, "{\"pattern\":%s,\"repeat\":%d}", pattern, repeat);
    else snprintf(body, size, "{\"pattern\":%s}", pattern);
    int status = ps5_signal_request("POST", path, body);
    free(body);
    return status;
}

int ps5_signal_running(void) { return ps5_signal_request("GET", "/status", "") == 200; }
int ps5_signal_beep(const char *pattern) { return ps5_signal_pattern("/beep", pattern, 0); }
int ps5_signal_led(const char *pattern, int repeat) { return ps5_signal_pattern("/led", pattern, repeat); }
int ps5_signal_color(int blue, int white, int amber) {
    char body[64];
    snprintf(body, sizeof(body), "{\"blue\":%d,\"white\":%d,\"amber\":%d}", blue, white, amber);
    return ps5_signal_request("POST", "/color", body);
}
int ps5_signal_color_system(void) { return ps5_signal_request("POST", "/color", "{\"system\":true}"); }
int ps5_signal_stop(void) { return ps5_signal_request("POST", "/stop", "{}"); }
#endif /* PS5_SIGNAL_IMPLEMENTATION */
#endif /* PS5_SIGNAL_H */
