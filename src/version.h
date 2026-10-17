/* ps5-signal's version, in /status and as a marker in the ELF file itself: Signal Companion reads the marker from
 * a copy (in ps5-autoloader's folder, say) to tell whether it is older than the one it carries. */
#pragma once

#define SIGNAL_VERSION "0.2.0"
#define SIGNAL_VERSION_MARKER "PS5SIGNAL_VER:" SIGNAL_VERSION
