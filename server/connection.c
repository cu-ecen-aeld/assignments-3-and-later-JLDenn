
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/socket.h>
#include <stdio.h>
#include <syslog.h>
#include <signal.h> 
#include <pthread.h>
#include "../aesd-char-driver/aesd_ioctl.h"

#include "linklist.h"
#include "main.h"

#define COMMAND_SEEKTO	"AESDCHAR_IOCSEEKTO:"


/***********************************************************************************
 *	The function called when a new client is connected
 **********************************************************************************/
void *processConnection(void *arg){
	
	//Extract the content we'll need for this connection
	ll_t *connectionItem = (ll_t*) arg;
	
	DEBUG_PRINT("--Starting thread-per-connection\n");
	
	DEBUG_PRINT("--Allocating the first memory block\n");
	//Assume a memory size, and we'll keep reading that amount until we get to the \n
	unsigned char *recvData = malloc(BLOCK_SIZE);
	if(!recvData){
		syslog(LOG_ERR, "malloc: %s", strerror(errno));
		close(connectionItem->socket);
		return NULL;
	}
	int dataSize = BLOCK_SIZE;
	int dataLen = 0;
	int outf = -1;
	ssize_t byteCount;
	
	DEBUG_PRINT("--Starting data receive loop\n");
	while(1){
		//Receive some data from the client
		DEBUG_PRINT("--recieving the next block of data @ offset %i, (buffer size: %i)\n", dataLen, dataSize);
		byteCount = recv(connectionItem->socket, recvData + dataLen, dataSize - dataLen, 0);
		if(byteCount == -1){
			if(errno == EAGAIN || errno == EINTR)
				continue;
			
			//All other errors are terminal,
			goto cleanupFail;
		}
		
		dataLen += byteCount;
		
		//Find the \n, it is there yet
		DEBUG_PRINT("--Searching for the '\\n' character\n");
		char* nl = memchr(recvData, '\n', dataLen);
		if(!nl){
			DEBUG_PRINT("--'\\n' not found, we need to receive more data\n");
			
			//If we received a 0 (EOF), we're done without a full packet, so we'll ignore it
			if(!byteCount){
				syslog(LOG_ERR, "We did not receive a full packet from the client, dropping");
				goto cleanupFail;
			}
			
			//We need more data, check if there is any room left.
			if(dataSize > dataLen)
				continue;
			
			
			//We need more room. Realloc and copy the existing data to the new memblock
			DEBUG_PRINT("--Realloc'ing the buffer to store more data\n");
			unsigned char *tmp = malloc(dataSize + BLOCK_SIZE);
			if(!tmp){
				syslog(LOG_ERR, "malloc: %s", strerror(errno));
				goto cleanupFail;
			}
			dataSize += BLOCK_SIZE;
			
			
			//Copy the data we want to keep to the newly allocated larger memory, and free
			//	the original buffer.
			DEBUG_PRINT("--Moving the data to the new buffer\n");
			memcpy(tmp, recvData, dataLen);
			free(recvData);
			recvData = tmp;
			
			//We're ready to try reading more data now, so we'll go back to the top of the while.
			continue;
		}
		
			
		
		//We want to get the length we're concerned with (might not be the full data length)
		DEBUG_PRINT("--Received a full packet (found '\\n' char)\n");
		dataLen = (unsigned long)nl - (unsigned long)recvData + 1;
		

		
		
		//Open the output file since we're ready to write a full line
		DEBUG_PRINT("--Opening the output file\n");
		outf = open(FILE_PATH, O_APPEND | O_RDWR | O_CREAT, 0644);
		if(outf == -1){
			syslog(LOG_ERR, "open %s: %s", FILE_PATH, strerror(errno));
			goto cleanupFail;
		}
	
		
#if !USE_AESD_CHAR_DEVICE
		//Obtain the mutex used to maintain file write integrety.
		DEBUG_PRINT("--Locking the mutex for file write access\n");
		int rc=pthread_mutex_lock(connectionItem->mutex);
		if(rc){
			errno = rc;
			perror("pthread_mutex_lock");
			goto cleanupFail;
		}
		
#else	// meaning USE_AESD_CHAR_DEVICE == 1
		DEBUG_PRINT("--Checking if packet is a defined command\n");
		if(!memcmp(recvData, COMMAND_SEEKTO, sizeof(COMMAND_SEEKTO)-1)){
			struct aesd_seekto st;
			if(sscanf((char*)recvData + sizeof(COMMAND_SEEKTO) - 1, "%u,%u", &st.write_cmd, &st.write_cmd_offset) != 2){
				//Overwrite the \n with a \0 so we can include it in the error log entry.
				recvData[dataLen-1] = '\0';
				syslog(LOG_ERR, "seekto command format invalid: %s", recvData);
				goto cleanupFailInLock;
			}
			
			DEBUG_PRINT("--Sending ioctl comamnd with st: %u, %u\n", st.write_cmd, st.write_cmd_offset);
			if(ioctl(outf, AESDCHAR_IOCSEEKTO, &st)){
				//In this case, we'll print an error to the screen, but not return anything to the client. 
				//This is the case where the command is properly formated, but the numbers are invalid, and
				//	since we don't have a better way to let the remote client know, we'll just send back nothing.
				perror("aesdchar_seekto");
				goto cleanupFailInLock;
			}
		}
		else{

#endif
		
		//Write the data to the file we opened above
		DEBUG_PRINT("--Writing data to the output file %s\n", FILE_PATH);
		byteCount = write(outf, recvData, dataLen);
		if(byteCount != dataLen){
			syslog(LOG_ERR, "write to %s failed", FILE_PATH);
			goto cleanupFailInLock;
		}
		
		//Now rewind and read the entire file so we can send it
		lseek(outf, 0, SEEK_SET);
		
#if USE_AESD_CHAR_DEVICE
		}
#endif
		
		
		//Loop until we get all the data in the file sent off to the client
		//	We'll be re-using the buffer we malloc'd above since we have that
		//	buffer already. We continually loop until we've read and sent each 
		//	block.
		do{
			//Read as much data as we can based on the size of our recvData buffer.
			DEBUG_PRINT("--Reading data back in from the file\n");
			byteCount = read(outf, recvData, dataSize);
			if(!byteCount)
				break;
			
			//Handle error cases
			if(byteCount < 0){
				if(errno == EAGAIN || errno == EINTR)
					continue;
				
				syslog(LOG_ERR, "read: %s", strerror(errno));
				goto cleanupFailInLock;
			}
			
			//Send the data we have in the buffer to the client
			DEBUG_PRINT("--Sending file data to client\n");
			ssize_t bytesSent = send(connectionItem->socket, recvData, byteCount, 0);
			if(bytesSent != byteCount){
				syslog(LOG_ERR, "error sending %li bytes to client", byteCount);
				goto cleanupFailInLock;
			}
		}while(byteCount);
		
#if !USE_AESD_CHAR_DEVICE		
		//Release the mutex
		DEBUG_PRINT("--Unocking the mutex\n");
		rc=pthread_mutex_unlock(connectionItem->mutex);
		if(rc){
			errno = rc;
			perror("pthread_mutex_unlock");
			goto cleanupFail;
		}
#endif
		
		
		DEBUG_PRINT("--Closing file\n");
		close(outf);
		break;
	}

	//Ensure we shutdown the connection to properly before we close it (notifying the client 
	//	we're done sending data)
	if(shutdown(connectionItem->socket, SHUT_RDWR))
		syslog(LOG_ERR, "shutdown: %s", strerror(errno));
	
	//Close our handle and log that we're done with this client
	close(connectionItem->socket);
	uint32_t clientIP = connectionItem->ip;
	syslog(LOG_INFO, "Closed connection from %u.%u.%u.%u",
				clientIP >> 24, (clientIP >> 16) & 0xFF, (clientIP >> 8) & 0xFF, clientIP & 0xFF);
	
	//Free our buffer and exit
	free(recvData); 
	return NULL;
	

//On fail inside the locked mutex area, we need to unlock that before we exit!
cleanupFailInLock:
#if !USE_AESD_CHAR_DEVICE
	rc=pthread_mutex_unlock(connectionItem->mutex);
	if(rc){
		errno = rc;
		perror("pthread_mutex_unlock");
	}
#endif

//On fail, we need to complete cleanup
cleanupFail:
	if(outf != -1)
		close(outf);
	free(recvData);
	close(connectionItem->socket);
	return NULL;
}