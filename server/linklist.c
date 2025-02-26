#include <stdio.h>
#include <stdlib.h>
#include "linklist.h"


/////////////////////////////////////////////////////////////
static ll_t *head = NULL;
static ll_t *ittr = NULL;

/////////////////////////////////////////////////////////////
void ll_dropItem(ll_t *dropLi){
	ll_t *li = head;
	ll_t *prevLi = NULL;
	
	
	while(li){
		if(li == dropLi){
			if(prevLi)
				prevLi->next = li->next;
			else{
				head = li->next;
				prevLi = head;
			}
			
			//Move the ittr up the list if it is pointing here. 
			if(ittr == li)
				ittr = prevLi;
			
			free(li);
			return;
		}
		
		prevLi = li;
		li = li->next;
	}
	return;
}

/////////////////////////////////////////////////////////////
ll_t *ll_addItem(){
	ll_t *li = malloc(sizeof(ll_t));
	if(!li){
		perror("malloc");
		return NULL;
	}

	//We'll insert the new list item at the head (since it is far easier)
	li->next = head;
	head = li;
	
	//Reset the itterator since we just added an item. This invalidates the itteration
	ittr = NULL;
	return head;
}

/////////////////////////////////////////////////////////////
ll_t *ll_getFirst(){
	if(!head)
		return NULL;
	
	ittr = head;
	return head;
}

/////////////////////////////////////////////////////////////
ll_t *ll_getNext(){
	if(!head || !ittr)
		return NULL;
	
	ittr = ittr->next;
	return ittr;
}

#ifdef DEBUG
void ll_printChain(){
	ll_t *li = head;
	int i = 0;
	while(li){
		printf("[%02i] Thread: %lu, socket: %i, \n", i, li->thread, li->socket);
		li = li->next;
		i++;
	}
}
#endif
