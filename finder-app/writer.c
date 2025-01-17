//-----------------------------------------------------------------
// @file		writer.c
// @author	 	Justin Denning, justin.denning@colorado.edu
// @date		2025-01-16
// @course		ECEN5713 - Spring 2025
// @assignment Assignment 2 - File Operations and Cross Compiler
//-----------------------------------------------------------------

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#define LOG_IDENT		"writer"

//Start the application
int main(int argc, char* argv[]){
	
	openlog(LOG_IDENT, 0, LOG_USER);
	
	if(argc != 3){
		printf("Usage: writer /path/to/file/to/write \"string to write\"\n");
		syslog(LOG_USER | LOG_ERR, "Invalid inputs provided");
		return 1;
	}
	
	//Save the filename and string to write so we can access them easier later (and we don't need to
	//	remember which argv index they are stored in)
	const char* filename = argv[1];
	const char* stringToWrite = argv[2];
	
	//printf("We'll attempt to open %s for writing, then write \"%s\" to it\n", filename, stringToWrite);
	syslog(LOG_USER | LOG_DEBUG, "Writing %s to %s", stringToWrite, filename);
	
	int fd = open(filename, O_WRONLY | O_CREAT,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if(fd == -1){
		printf("Error opening %s for writing\n", filename);
		syslog(LOG_USER | LOG_ERR, "Error opening %s for writing", filename);
		return 1;
	}
	
	//Get the length of the string we'll be writing to the file
	size_t toWrite = strlen(stringToWrite);
	
	//Attempt to write to the given file. We'll handle an error if we don't end up writing all the date we wanted.
	ssize_t written = write(fd, stringToWrite, toWrite);
	if(written != toWrite){
		printf("Error writing some or all of the bytes to %s (wrote %li out of %lu)\n", filename, written, toWrite);
		syslog(LOG_USER | LOG_ERR, "Error writing some or all of the bytes to %s (wrote %li out of %lu)\n", filename, written, toWrite);
		close(fd);
		return 1;
	}
	
	//Close the file
	close(fd);
	
	return 0;
}