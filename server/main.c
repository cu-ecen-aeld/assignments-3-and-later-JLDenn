
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

#include "connection.h"
bool closeApplication = false;
pthread_mutex_t fileWriteMutex;

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
	struct timespec t = {.tv_sec = 0, .tv_nsec = 1000000}; //1ms
	while(waitpid(-1, NULL, WNOHANG) >= 0)
		nanosleep(&t, NULL);
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
	
	//We have bound to the requested port, we can fork at this point if we're running as a daemon
	if(runDaemon){
		DEBUG_PRINT("Shifting to daemon\n");
		//setup and fork. If a pid is returned (!= 0), we're the parent, and need to return.
		if(makeDaemon())
			return 0;
	}
	
	
	//Start the periodic timer to handle the 10second timestamp writes
	timer_t *timerId = intervalTimerStart(&fileWriteMutex);
	

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
		if(!fork()){
			processConnection(clientSocket, ip, &fileWriteMutex);
			//Should never get here since the processConnection should exit when complete
			exit(EXIT_FAILURE);
		}
		
		//Wait on any child threads that are complete. We don't actually wait, we
		//	just collect any that are complete already (to de-zombie them)
		while(waitpid(-1, NULL, WNOHANG) > 0)
			;
		
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