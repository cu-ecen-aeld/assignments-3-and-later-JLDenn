#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>


//#define DEBUG

// Optional: use these functions to add debug or error prints to your application
#ifdef DEBUG
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#else
#define DEBUG_LOG(msg,...)
#endif
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)



void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
	int ret;
	
	struct thread_data *tdata = (struct thread_data*) thread_param;
	
	//Sleep before we obtain mutex lock
	DEBUG_LOG("Sleeping for %ims prior to locking the mutex", tdata->wait_to_obtain_ms);
	struct timespec sleep_time = {tdata->wait_to_obtain_ms / 1000, 
									(tdata->wait_to_obtain_ms % 1000) * 1000000};
	ret = nanosleep(&sleep_time, NULL);
	if(ret){
		ERROR_LOG("Error sleeping");
		tdata->thread_complete_success = false;
		return thread_param;
	}
	
	//Obtain the lock
	DEBUG_LOG("Locking the mutex");
	pthread_mutex_lock(tdata->mutex);
	
	
	//Sleep before we release the lock
	DEBUG_LOG("Sleeping for %ims before unlocking the mutex", tdata->wait_to_release_ms);
	sleep_time.tv_sec = tdata->wait_to_release_ms / 1000;
	sleep_time.tv_nsec = (tdata->wait_to_release_ms % 1000) * 1000000;
	ret = nanosleep(&sleep_time, NULL);
	if(ret){
		ERROR_LOG("Error sleeping");
		pthread_mutex_unlock(tdata->mutex);
		tdata->thread_complete_success = false;
		return thread_param;
	}
	
	//Release the lock
	DEBUG_LOG("Unlocking the mutex");
	pthread_mutex_unlock(tdata->mutex);
	
	//Mark thread as completed successfully
	tdata->thread_complete_success = true;
	
	//Return the data structure we were passed
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
	 */
	int ret;
	
	//We're assuming the mutex is already initialized!!!!
	
	//Allocate memory for the structure we'll be using to pass the parameters to the thread
	struct thread_data *tdata = malloc(sizeof(struct thread_data));
	if(!tdata){
		ERROR_LOG("Unable to allocate memory for the threadParam structure");
		return false;
	}
	
	
	//Populate the structure we'll be passing to the thread
	tdata->wait_to_obtain_ms = wait_to_obtain_ms;
	tdata->wait_to_release_ms = wait_to_release_ms;
	tdata->mutex = mutex;
	tdata->thread_complete_success = false;
	
	//Create the new thread
	ret = pthread_create(thread,			//receive the new thread's ID
						NULL,				//Use the default thread attributes
						threadfunc,			//the function to start in the new thread
						(void*) tdata);		//The parameter passed to threadfunc
	if(ret){
		ERROR_LOG("Error creating the thread (error: %i)", ret);
		
		//Since the thread will not be running, we need to free tdata before we return
		free(tdata);
		return false;
	}

	//Return success
    return true;
}

