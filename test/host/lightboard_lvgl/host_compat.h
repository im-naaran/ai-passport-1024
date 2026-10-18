#pragma once
#ifdef __APPLE__
// The firmware's existing mode_t name collides with Darwin's filesystem typedef.
// Load system declarations under a host-only name before application headers.
#define mode_t lightboard_host_filesystem_mode_t
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#undef mode_t
#endif
