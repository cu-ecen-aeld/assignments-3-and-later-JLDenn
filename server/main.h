
#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

#define LISTEN_PORT		9000
#define MAX_BACKLOG 	100

#undef EXIT_FAILURE
#define EXIT_FAILURE	-1

#define BLOCK_SIZE		64
#define FILE_PATH		"/var/tmp/aesdsocketdata"  

#define TIMESTAMP_INTERVAL_S	10


//Comment this to remove the verbose prints
//#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(a, ...) printf(a, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(a, ...)
#endif

//Extern functions
timer_t *intervalTimerStart(pthread_mutex_t *fileWriteMutex);


#endif 	//#define MAIN_H
