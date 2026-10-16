/* Host tests: the kernel log goes to stderr. */
#pragma once
#include <stdio.h>
#define klog_printf(...) fprintf(stderr, __VA_ARGS__)
