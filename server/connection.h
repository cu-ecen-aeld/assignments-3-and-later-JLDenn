
#ifndef CONNECTION_H
#define CONNECTION_H

#include <pthread.h>

//External function that handles connections
void *processConnection(void *arg);

#endif //CONNECTION_H