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

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Justin Denning");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

	//Get our dev structure
	struct aesd_dev *dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	
	//Assign the filp's private_data to our struct (so we can get to it later)
	filp->private_data = dev;
	
    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

	//Get our dev structure
	struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
	
	//Free the partial buffer if needed
	if(mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;
	
	if(dev->partialBuffer){
		kfree(dev->partialBuffer);
		dev->partialBuffer = NULL;
		dev->partialBufferSize = 0;
	}
	mutex_unlock(&dev->mutex);
	
	//We will be leaving the rest of the circular buffer and its data since it may be accessed
	//	again. It will be freed during cleanup.

    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

	//Get our dev structure
	struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
	
	PDEBUG("Getting the mutex lock");
	if(mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;
	
	
	size_t offset;
	struct aesd_buffer_entry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->cirBuf, *f_pos, &offset);
	if(!entry){
		//We're out of data, so we'll return 0;
		goto retValReady;
		
	size_t toCopy = MIN(count, entry->size - offset);
	unsigned long res = copy_to_user(buf, entry->buffptr + offset, toCopy);	
	
	//If res == 0 (success), retval == toCopy. Otherwise retval = toCopy - res (which is the same in both cases):
	retval = toCopy - res;
	*f_pos += retval;

		
retValReady:
	mutex_unlock(&dev->mutex);
    return retval;
}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
	
	//Get our dev structure
	struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;

	PDEBUG("Getting the mutex lock");
	if(mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;

	//The partialBuffer currently contains data, we'll be appending to that (krealloc)
	if(dev->partialBuffer){
		PDEBUG("We have a partial packet in work (%i bytes), we'll be adding more to it (%i bytes).", 
				dev->partialBufferSize, count);
				
		char* tmp = krealloc(dev->partialBuffer, dev->partialBufferSize + count, GFP_KERNEL);
		if(!tmp)
			goto retValReady;
		
		dev->partialBuffer = tmp;
		dev->partialBufferSize += count;
	}
	//No partial data is stored, we'll start a new packet
	else{
		PDEBUG("No data currently in the partial buffer, so we'll allocate memory for the block we received (%i bytes).", count);
		
		dev->partialBuffer = kmalloc(count);
		if(!dev->partialBuffer)
			goto retValReady;
		
		dev->partialBufferSize = count;
	}

	PDEBUG("Copying %i bytes from user space to our partialBuffer.", count);
	//Copy from userspace. If we get a value != 0, that is how many bytes WEREN'T copied.
	//	So, we'll return the number of bytes we could copy, leaving the partialBuffer size alone.
	unsigned long res = copy_from_user(dev->partialBuffer, buf, count);
	if(res){
		retval = count - res;
		goto retValready;
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
	char* old = aesd_circular_buffer_add_entry(&dev->cirBuf, &entry);
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
int aesd_init_module(void)
{
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

	//aesd_dev.partialBufferSize		//already set to 0
	//aesd_dev.partialBuffer			//already NULL
	mutex_init(&aesd_dev.mutex);		//no return value
	//aesd_dev.cirBuf					//already configured as empty (= {0})

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);


	//We need to run through and free all the allocated memory from the cirBuf
	uint8_t index;
	AESD_CIRCULAR_BUFFER_FOREACH(struct aesd_buffer_entry *entry, aesd_device.cirBuf, index)
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
