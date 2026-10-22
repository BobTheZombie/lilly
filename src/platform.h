/* SPDX-License-Identifier: MIT */
#ifndef LILLY_PLATFORM_H
#define LILLY_PLATFORM_H
void platform_early(void);
_Noreturn void platform_emergency(void);
_Noreturn void platform_shutdown(int restart);
int platform_join(const char *name);
void platform_kill(const char *name);
#endif
