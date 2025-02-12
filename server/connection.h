
#ifndef CONNECTION_H
#define CONNECTION_H

#include <pthread.h>

//External function that handles connections
void processConnection(int clientSocket, uint32_t clientIP, pthread_mutex_t *fileWriteMutex);

#endif //CONNECTION_H