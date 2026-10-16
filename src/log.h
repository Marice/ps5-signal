/* The service's log: the kernel log (klogsrv), prefixed with [ps5-signal]. */
#pragma once

#include <ps5/klog.h>

#define log_printf(...) klog_printf("[ps5-signal] " __VA_ARGS__)
