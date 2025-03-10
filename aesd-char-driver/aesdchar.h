/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#include "aesd_circular_buffer.h"

#define AESD_DEBUG 1  //Remove comment on this line to enable debug

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

struct aesd_dev
{
    /**
     * TODO: Add structure(s) and locks needed to complete assignment requirements
     */
	char *partialBuffer;		//Holds the partial buffer we'll use to store incomplete packets
	uint32_t partialBufferSize;	//Holds the current allocated size of the partial buffer.
	struct mutex mutex;			//holds the concurancy mutex for critical sections reguarding buffer and tree modifications
	
	struct aesd_circular_buffer cirBuf	//The actual buffer that holds 10 entries
	
    struct cdev cdev;     		/* Char device structure      */
};


#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
