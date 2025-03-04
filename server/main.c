
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <syslog.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include "main.h"
#include "linklist.h"

#include "connection.h"
static bool closeApplication = false;
static pthread_mutex_t fileAccessMutex;

/***********************************************************************************
 *	The signal callback handler. 
 * 
 * 	Since we're ONLY writing, and the main code ONLY reads, we don't have to worry
 *	about data race conditions (so no critical section)
 **********************************************************************************/
static void onSignal(int signum){
	closeApplication = true;
}


/***********************************************************************************
 *	Wait for all the child processes to complete and exit
 **********************************************************************************/
static void waitForChildren(){
	//Wait for any child processes that are still processing	
	ll_t *li = ll_getFirst();
	while(li){
		pthread_join(li->thread, NULL);
		ll_dropItem(li);
		li = ll_getFirst();
	}
}

/***********************************************************************************
 *	Configure the code in preperation for switching to daemon mode.
 **********************************************************************************/
static int makeDaemon(){
	
	if(chdir("/")){
		syslog(LOG_ERR, "chdir: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	int devNull = open("/dev/null", O_WRONLY);
	dup2(devNull, STDOUT_FILENO);
	dup2(devNull, STDERR_FILENO);
	
	return fork();
}


/***********************************************************************************
 *	Main server application code
 **********************************************************************************/
int main(int argc, char* argv[]){
	
	
	//Check arguments to see if we're supposed to run in daemon mode.
	bool runDaemon = false;
	if(argc == 2 && !strcmp("-d" , argv[1]))
		runDaemon = true;
	else if(argc != 1){
		printf("Usage: %s [-d]\nAdding -d will run the app as a daemon\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	//Setup the syslog system so we can write to the syslog
	setlogmask (LOG_UPTO (LOG_INFO));
	openlog(argv[0], LOG_CONS | LOG_PID | LOG_NDELAY | LOG_PERROR ,LOG_USER);
	syslog(LOG_INFO, "Starting aesdsocket server");
	
	//Register the two signal handlers we need to listen for
	DEBUG_PRINT("Registering signal handlers\n");
	struct sigaction act = {.sa_handler = onSignal};
	if(sigaction(SIGINT, &act, NULL)){
		syslog(LOG_ERR, "sigaction: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	if(sigaction(SIGTERM, &act, NULL)){
		syslog(LOG_ERR, "sigaction: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	//Create the socket we'll be using to receive client connections
	DEBUG_PRINT("Creating socket\n");
	int socketHandle;
	socketHandle = socket(PF_INET, SOCK_STREAM, 0);
	if(socketHandle == -1){
		syslog(LOG_ERR, "socket: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	//Ensure we can immediatly reuse the port as soon as we exit (for future program invocations)
	//	I was running into issues with the socket-test.sh since it invokes it multiple times. 
	int ret = setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
	if(ret < 0){
		syslog(LOG_ERR, "setsockopt: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	//Bind our socket to the desired LISTEN_PORT
	struct sockaddr_in addr_in = {
						.sin_family = AF_INET, 
						.sin_addr = {.s_addr = INADDR_ANY}, 
						.sin_port = htons(LISTEN_PORT)
						};
	
	DEBUG_PRINT("Binding socket\n");
	ret = bind(socketHandle, (struct sockaddr*)&addr_in, sizeof(addr_in));
	if(ret < 0){
		syslog(LOG_ERR, "bind: %s", strerror(errno));
		close(socketHandle);
		exit(EXIT_FAILURE);
	}
	
	//Inialize the mutex we'll be using for file access cohesion
	ret = pthread_mutex_init(&fileAccessMutex, NULL);
	if(ret){
		perror("pthread_mutex_init");
		close(socketHandle);
		exit(EXIT_FAILURE);
	}
	
	//We have bound to the requested port, we can fork at this point if we're running as a daemon
	if(runDaemon){
		DEBUG_PRINT("Shifting to daemon\n");
		//setup and fork. If a pid is returned (!= 0), we're the parent, and need to return.
		if(makeDaemon())
			return EXIT_SUCCESS;
	}
	
	
	//Start the periodic timer to handle the 10second timestamp writes
	timer_t *timerId = intervalTimerStart(&fileAccessMutex);
	

	//Begin listening for client connections
	DEBUG_PRINT("Listening\n");
	ret = listen(socketHandle, MAX_BACKLOG);
	if(ret){
		syslog(LOG_ERR, "listen: %s", strerror(errno));
		close(socketHandle);
		exit(EXIT_FAILURE);
	}

	//Loop until we've been told to exit (by a signal)
	struct sockaddr addr;
	while(!closeApplication){
		
		//Potential "livelock" situation between the !closeApplication check and
		//	the accept() call below. If we receive a SIGINT/SIGTERM between these two
		//	lines, we'll be stuck in accept until a client connects to us...
		//	but i'm not sure how to fix that....
	
		//We can now block until an incomming connection is received. 
		DEBUG_PRINT("Waiting for new connection...\n");
		socklen_t addrLen = sizeof(addr);
		int clientSocket = accept(socketHandle, &addr, &addrLen);
		if(clientSocket == -1){

			//Recoverable error or signal request to close
			if(errno == EAGAIN || errno == ECONNABORTED || errno == EINTR)
				continue;
			
			//We'll get here if any unrecoverable error occurs
			syslog(LOG_ERR, "accept: %s", strerror(errno));
			
			close(socketHandle);
			
			waitForChildren();
			exit(EXIT_FAILURE);
		}
		
		//Save the IP address of the connecting client
		uint32_t ip = ntohl(*(uint32_t *)&addr.sa_data[2]);
		
		DEBUG_PRINT("Connection accepted\n");
		//We have a connection, Log who we are connected to
		syslog(LOG_INFO, "Accepted connection from %u.%u.%u.%u", 
				ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
				
				
		
				
		//We will now create a child process to handle the connection we just made.
		//	This allows us to wait for more connections while we're still processing
		//	a previous one. 
		DEBUG_PRINT("Spawning child to handle request\n");
		ll_t *li = ll_addItem();
		if(!li){
			syslog(LOG_ERR, "Unable to allocate arg content for new client thread");
			close(clientSocket);
		}
		else{
			li->ip = ip;
			li->socket = clientSocket;
			li->mutex = &fileAccessMutex;
			
			ret = pthread_create(&li->thread, NULL, processConnection, li);
			if(ret){
				perror("pthread_create");
				ll_dropItem(li);
				close(clientSocket);
				syslog(LOG_ERR, "Unable to create child thread for client connection");
			}
		}

		
		
		//Wait on any child threads that are complete. We don't actually wait, we
		//	just collect any that are complete already (to de-zombie them). In other
		//	words we'll be ignoring the return values.
		//
		//	By checking for complete children after every new child, we ensure we don't 
		//	hold as large of list of threads that need joined, and frees their resources
		//	sooner. 
		//
		li = ll_getFirst();
		while(li){
			DEBUG_PRINT("Waiting for child 0x%08x\n", (unsigned int)li->thread);
			if(!pthread_tryjoin_np(li->thread, NULL)){
				DEBUG_PRINT("Child 0x%08x has completed\n", (unsigned int)li->thread);
				
				//The thread has exited, so we'll drop (free) the link list item
				ll_dropItem(li);
			}

			li = ll_getNext();
		}
		
		
		//As the parent we continue processing more connections
	}
	
	//We must have received a signal (where we set the closeApplication), so we're ready to exit.
	syslog(LOG_INFO, "Caught signal, exiting");
	printf("\nWaiting for connections to complete, and exiting...\n");
	
	//Cancel the timer
	timer_delete(timerId);
	
	waitForChildren();
	close(socketHandle);
	
	//Delete the data file
	unlink(FILE_PATH);
	
	closelog();
	exit(EXIT_SUCCESS);
}