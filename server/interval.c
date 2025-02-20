#include <stdio.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <syslog.h>
#include "main.h"

//Structure used to pass data to the timer IRQ handler.
struct timerData{
        int eventNumber;
		pthread_mutex_t *fileAccessMutex;
};

/**************************************************************************************/
// Periodic timer (every 10 seconds) allowing us to add timestamps to our output file.
//
//	Runs from interval timer call
void onInterval(union sigval arg){
	
	int rc;
	struct timerData *td = arg.sival_ptr;
	int outf;
	char timeStr[128];
	
	//Increment the number of calls we've handled
	td->eventNumber++;
	DEBUG_PRINT("---Handling event %i\n", td->eventNumber);
	
	time_t rawTime;
	time(&rawTime);
	struct tm *locTime = localtime(&rawTime);	//Returns static struct, no need to free
	size_t timeStrLen = strftime(timeStr, sizeof(timeStr), "timestamp:%Y-%m-%d %H:%M:%S\n", locTime);
	if(!timeStrLen){
		printf("Error generating time string\n");
		exit(EXIT_FAILURE);
	}

	//Open the output file
	DEBUG_PRINT("---Opening the output file\n");
	outf = open(FILE_PATH, O_APPEND | O_RDWR | O_CREAT, 0644);
	if(outf == -1){
		syslog(LOG_ERR, "open %s: %s", FILE_PATH, strerror(errno));
		goto cleanupFail;
	}

	//Obtain mutex for file writes
	DEBUG_PRINT("---Locking the write file\n");
	rc=pthread_mutex_lock(td->fileAccessMutex);
	if(rc){
		errno = rc;
		perror("pthread_mutex_lock");
		goto cleanupFail;
	}
	
	//Write the data to the file we opened above
	DEBUG_PRINT("---Writing timestamp to the output file %s\n", FILE_PATH);
	ssize_t byteCount = write(outf, timeStr, timeStrLen);
	if(byteCount != timeStrLen){
		syslog(LOG_ERR, "write to %s failed", FILE_PATH);
		goto cleanupFailInLock;
	}
	
	DEBUG_PRINT("---Unlocking the write file\n");
	rc=pthread_mutex_unlock(td->fileAccessMutex);
	if(rc){
		errno = rc;
		perror("pthread_mutex_unlock");
		goto cleanupFail;
	}
	
	close(outf);
	return;
	
//On fail inside the locked mutex area, we need to unlock that before we exit!
cleanupFailInLock:
	rc=pthread_mutex_unlock(td->fileAccessMutex);
	if(rc){
		errno = rc;
		perror("pthread_mutex_unlock");
	}	
cleanupFail:
	if(outf != -1)
		close(outf);
	exit(EXIT_FAILURE);
}




/**************************************************************************************/
// Function to setup the interval timer that will place timestamps in the output file
timer_t *intervalTimerStart(pthread_mutex_t *fileAccessMutex){
	
	//Configure the timer we'll be using to interrupt at our time slice (10ms)
	timer_t timerId = 0;
	//Initially start eventNumber negative so the timer has time to even out before we
	//      actually start the scheduler period.
	static struct timerData tData = {0};
	tData.eventNumber = 0;
	tData.fileAccessMutex = fileAccessMutex;
	
	struct sigevent sev = {0};
	struct itimerspec ts = {.it_value.tv_sec = 0,
							.it_value.tv_nsec = 1000000,	//1ms initially so we'll have a start of file timestamp
							.it_interval.tv_sec = TIMESTAMP_INTERVAL_S,
							.it_interval.tv_nsec = 0};

	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_notify_function = onInterval;
	sev.sigev_value.sival_ptr = &tData;

	//Create the timer.
	int rc=timer_create(CLOCK_REALTIME, &sev, &timerId);
	if(rc){
			perror("timer_create");
			exit(EXIT_FAILURE);
	}
	//Start the timer as configured. It will now call our scheduler() function every 10ms
	rc=timer_settime(timerId, 0, &ts, NULL);
	if(rc){
			perror("timer_settime");
			exit(EXIT_FAILURE);
	}

	return timerId;
}