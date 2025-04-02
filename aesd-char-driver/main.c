/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/mutex.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Justin Denning");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

// int aesd_open(struct inode *inode, struct file *filp);
// int aesd_release(struct inode *inode, struct file *filp);
// ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
// ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
// loff_t aesd_seek(struct file* filp, loff_t f_pos, int whence);
// int aesd_init_module(void);
// void aesd_cleanup_module(void);

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
static int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("-open");

	//Get our dev structure
	struct aesd_dev *dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	
	//Assign the filp's private_data to our struct (so we can get to it later)
	filp->private_data = dev;
	
    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
static int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("-release");

	//Get our dev structure
	//struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
	
	//We will be leaving the rest of the circular buffer and its data since it may be accessed
	//	again. It will be freed during cleanup.

    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
static long aesd_ioctl (struct file *filp, unsigned int cmd, unsigned long argp){
	
	PDEBUG("-ioctl, cmd = %u",cmd);
	
	//Default to bad value so we can just fall through in that case.
	long retVal = -EINVAL;
	
	//Get our dev structure
	struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
	
	switch(cmd){
		case AESDCHAR_IOCSEEKTO:{
		
			PDEBUG("Received a seekto cmd");
			//Get the seekto structure from user space.
			struct aesd_seekto st;
			if(copy_from_user(&st, (void*)argp, sizeof(st)))
				return -EFAULT;
			
			PDEBUG("write_cmd = %u, write_cmd_offset = %u", st.write_cmd, st.write_cmd_offset);
		
			PDEBUG("Getting the mutex lock");
			if(mutex_lock_interruptible(&dev->mutex))
				return -ERESTARTSYS;
			
			int index;
			off_t cumulativeOffset = 0;
			struct aesd_buffer_entry *entry;
			
			//Loop through all the entries. If we end up at an index that matches write_cmd, we'll 
			//	check the write_cmd_offset and complete. But, if we never find write_cmd, we know
			//	it was invalid, so we'll finish the loop and exit with the inital value of -EINVAL set. 
			AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.cirBuf, index){
				if(	index == st.write_cmd ){
					//See if the cmd_offset is valid
					if(st.write_cmd_offset >= entry->size)
						break;
					
					//We have a valid location, set the f_pos and return;
					filp->f_pos = cumulativeOffset + st.write_cmd_offset;
					retVal = 0;
					break;
				}
				else {
					cumulativeOffset += entry->size;
				}
			}	


			PDEBUG("Unlocking the mutex");
			mutex_unlock(&dev->mutex);			
			
			break;}
	}


	return retVal;
}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
static ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("-read %zu bytes with offset %lld",count,*f_pos);

	//Get our dev structure
	struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
	
	PDEBUG("Getting the mutex lock");
	if(mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;
	
	
	PDEBUG("Seaching circular buffer for offset %lld", *f_pos);
	size_t offset;
	struct aesd_buffer_entry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->cirBuf, *f_pos, &offset);
	if(!entry){
		//We're out of data, so we'll return 0;
		goto retValReady;
	}
		
	
	size_t toCopy = min(count, entry->size - offset);
	
	PDEBUG("Found entry, %lu offset. We'll copy %lu bytes to user space", offset, toCopy);
	unsigned long res = copy_to_user(buf, entry->buffptr + offset, toCopy);	
	
	//If res == 0 (success), retval == toCopy. Otherwise retval = toCopy - res (which is the same in both cases):
	retval = toCopy - res;
	
	*f_pos += retval;
	PDEBUG("Copy complete, updated f_pos: %lld", *f_pos);

	
retValReady:
	PDEBUG("Unlocking mutex");
	mutex_unlock(&dev->mutex);
    return retval;
}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
static ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("-write %zu bytes with offset %lld",count,*f_pos);
	size_t offset = 0;
	
	//Get our dev structure
	struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;

	PDEBUG("Getting the mutex lock");
	if(mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;

	//The partialBuffer currently contains data, we'll be appending to that (krealloc)
	if(dev->partialBuffer){
		PDEBUG("We have a partial packet in work (%lu bytes), we'll be adding more to it (%lu bytes).", 
				dev->partialBufferSize, count);
				
		char* tmp = krealloc(dev->partialBuffer, dev->partialBufferSize + count, GFP_KERNEL);
		if(!tmp)
			goto retValReady;
		
		dev->partialBuffer = tmp;
		offset = dev->partialBufferSize;
		dev->partialBufferSize += count;
		PDEBUG("New partialBufferSize: %lu", dev->partialBufferSize);
	}
	//No partial data is stored, we'll start a new packet
	else{
		PDEBUG("No data currently in the partial buffer, so we'll allocate memory for the block we received (%lu bytes).", count);
		
		dev->partialBuffer = kmalloc(count, GFP_KERNEL);
		if(!dev->partialBuffer)
			goto retValReady;
		
		dev->partialBufferSize = count;
	}

	PDEBUG("Copying %lu bytes from user space to our partialBuffer.", count);
	//Copy from userspace. If we get a value != 0, that is how many bytes WEREN'T copied.
	//	So, we'll return the number of bytes we could copy, leaving the partialBuffer size alone.
	unsigned long res = copy_from_user(dev->partialBuffer + offset, buf, count);
	if(res){
		retval = count - res;
		goto retValReady;
	}
	
	//We've written the entire contents of count at this point, so this will be our return value.
	retval = count;
	
	PDEBUG("Checking for '\\n'");
	//Search for \n character that would indicate a complete packet. 
	char *loc = memchr(dev->partialBuffer, '\n', dev->partialBufferSize);
	if(!loc)
		goto retValReady;	//Not found, we must be waiting for more data
		
		
	PDEBUG("Adding entire partialBuffer to the circularBuffer");
	//We have a \n in the partial buffer, we'll add it to the circular buffer
	struct aesd_buffer_entry entry = {.buffptr = dev->partialBuffer, 
										.size = (unsigned long)loc - (unsigned long)dev->partialBuffer + 1};
	const char* old = aesd_circular_buffer_add_entry(&dev->cirBuf, &entry);
	if(old){
		PDEBUG("The buffer was full, so we're freeing the dropped memory");
		
		//If we are returned a pointer, that memory needs to be freed since it was just dropped
		//	out of the buffer since it was too old, and the buffer was full.
		kfree(old);
	}
	
	//Since we passed the buffer to the circular buffer, we can clear the partial (The previous partial
	//	will be freed when the buffer gets too full (above), or during cleanup
	dev->partialBuffer = NULL;
	dev->partialBufferSize = 0;
	
		
retValReady:
	PDEBUG("Unlocking the mutex");
	mutex_unlock(&dev->mutex);
    return retval;
}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
static loff_t aesd_seek(struct file* filp, loff_t f_pos, int whence){
	
	PDEBUG("-seek to offset %lli from %d",f_pos, whence);
	
	//Get our dev structure
	struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
	
	loff_t newPos = 0;
	size_t dataSize = 0;
	
	//First we need to get the size of the data set, which we'll use for SEEK_END, and 
	//	range checking at the end.
	PDEBUG("Getting the mutex lock");
	if(mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;
	
	PDEBUG("Calculating current length");
	uint8_t index;
	struct aesd_buffer_entry *entry;
	AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->cirBuf, index)
		dataSize += entry->size;	
	
	
	//Now we'll set the newPos value based on where the user wants us to move to.
	switch(whence){
		case SEEK_SET:
			newPos = f_pos;
			break;
		case SEEK_CUR:
			newPos = filp->f_pos + f_pos;
			break;
		case SEEK_END:
			newPos = dataSize + f_pos;
			break;		
		default:
			//Here we'll just set to -1 so we'll return EINVAL after we release the mutex
			newPos = -1;
	}
	
	PDEBUG("User wishes to seek to %lli", newPos);
	
	//Check if the newPos is valid.
	if(newPos < 0 || newPos > dataSize){
		PDEBUG("That position is invalid: < 0 or > %li", dataSize);
		newPos = -EINVAL;
	}
	else{
		filp->f_pos = newPos;
	}

	PDEBUG("Unlocking the mutex");
	mutex_unlock(&dev->mutex);
	
	//Return the new position, or the error.
	return newPos;
}



////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
///								INIT/CLEANUP
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////




////////////////////////////////////////////////////////////////////////////////////////
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
	.llseek = 	aesd_seek,
	.unlocked_ioctl =	aesd_ioctl,
};

////////////////////////////////////////////////////////////////////////////////////////
static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
static int aesd_init_module(void)
{
	PDEBUG("-Initializing module (loading)");
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

	//aesd_device.partialBufferSize		//already set to 0
	//aesd_device.partialBuffer			//already NULL
	mutex_init(&aesd_device.mutex);		//no return value
	aesd_circular_buffer_init(&aesd_device.cirBuf);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
static void aesd_cleanup_module(void)
{
    PDEBUG("-Cleaning up module (unloading)");
	
	dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);
	


	//We need to run through and free all the allocated memory from the cirBuf
	uint8_t index;
	struct aesd_buffer_entry *entry;
	AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.cirBuf, index)
		kfree(entry->buffptr);
	
	//And free the partialBuffer if it was allocated
	if(aesd_device.partialBuffer)
		kfree(aesd_device.partialBuffer);
		//No need to update partialBuffer to NULL, or set partialBufferSize since they will
		//	not be used again after this.

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
