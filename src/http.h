/* The HTTP API: GET /status, POST /beep, POST /led, POST /stop, JSON in and out. */
#pragma once

#include <stdbool.h>

#define SIGNAL_PORT 9150

/* Opens the port; -1 when it can't (another instance runs). any_address listens on every interface instead
 * of 127.0.0.1 (the development build only). */
int http_listen(int port, bool any_address);
/* Answers requests on the socket from http_listen; doesn't return. */
void http_serve(int server, int port, bool any_address);
/* Asks http_serve to open its socket again: after rest mode (SIGCONT), the old one is dead. Signal-safe. */
void http_reopen(void);
