/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
		struct aesd_circular_buffer *buffer,
        size_t char_offset, 
		size_t *entry_offset_byte_rtn )
{
	
	DEBUG_PRINT("Find() request: char_offset: %lu, in_offs: %u, out_offs: %u, full: %c\n",
			char_offset, buffer->in_offs, buffer->out_offs, buffer->full ? 'T' : 'F');
	
	//Check if we have ANY entries first. If we don't, we return NULL (failure)
	if(buffer->out_offs == buffer->in_offs && !buffer->full)
		return NULL;
	
	
	uint8_t offs = buffer->out_offs;
	struct aesd_buffer_entry *entry = &buffer->entry[offs];
	
	//Loop as long as the char_offset requested is not in the current entry.
	while(char_offset >= entry->size){
		
		DEBUG_PRINT("Find() progress: char_offset: %lu, offs: %u (entry->size: %lu), in_offs: %u, out_offs: %u, full: %c\n",
				char_offset, offs, entry->size, buffer->in_offs, buffer->out_offs, buffer->full ? 'T' : 'F');
		
		//Reduce the char_offset by the size of this entry, and advance to the next one. 
		char_offset -= entry->size;
		offs = (offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

		//If offs is the same as the input offset (in_offs), we don't have any more entries to check, so
		//	we'll return NULL to indicate failure. 
		//	Since this check is performed after the offs increment above, it could only mean we've hit the 
		//	end (and we can't be at the beginning)
		if(offs == buffer->in_offs)
			return NULL;
		
		//Get the new entry we just moved to (since we know we didn't hit the end yet)
		entry = &buffer->entry[offs];
	}
	
	//If we're still looking at locations past this entry, we won't find it... return NULL (failure)
	if(char_offset >= entry->size)
		return NULL;
	
	DEBUG_PRINT("Find() conclusion: found entry (offs: %u, char_offset: %lu)\n", offs, char_offset);
	//At this point, the offset requested lands in this entry, so we'll return it and the byte offset.
	*entry_offset_byte_rtn = char_offset;
	return entry;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
const char * aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
	//Initially, we'll assume we're not full. If we are, we'll update this to the entry being replaced. 
	const char *retValue = NULL;
	if(buffer->full)
		retValue = buffer->entry[buffer->out_offs].buffptr;

	
	//Add the entry and increment the write pointer
	memcpy(&buffer->entry[buffer->in_offs], add_entry, sizeof(*add_entry));
	buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
	
	//If the buffer was already full, we need to advance the out_offs as well. Simply we can set them equal
	if(buffer->full)
		buffer->out_offs = buffer->in_offs;

	//If it wasn't full already, we'll check if it is now full. If so, we'll flag it as full
	else if(buffer->in_offs == buffer->out_offs)
		buffer->full = true;
	
	//Return NULL (if we aren't full), or the const char* to the memory buffer we're dropping (so it can be freed)
	return retValue;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
