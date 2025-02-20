#ifndef LINKLIST_H
#define LINKLIST_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include "main.h"

/////////////////////////////////////////////////////////////
typedef struct _ll_t {
	pthread_t thread;
	pthread_mutex_t *mutex;
	int socket;
	uint32_t ip;
	
	struct _ll_t *next;
}ll_t;


#ifdef DEBUG
void ll_printChain();
#endif

void ll_dropItem(ll_t *dropLi);
ll_t *ll_addItem();

ll_t *ll_getFirst();
ll_t *ll_getNext();


#endif