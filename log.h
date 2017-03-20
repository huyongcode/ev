#ifndef LOG_H_
#define LOG_H_

#include <stdio.h>

enum log_level {
	L_DEBUG_HUGE = 0,
	L_DEBUG,
	L_INFO,
	L_WARN,
	L_ERROR
};

extern enum log_level g_level;

#define LOG(level, fmt, ...) \
	do { \
		if ((level) >= g_level) \
			printf(#level ": " fmt, ##__VA_ARGS__); \
	} while(0)

#endif /* LOG_H_ */
