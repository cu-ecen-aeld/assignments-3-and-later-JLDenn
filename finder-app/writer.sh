#!/bin/bash
# writer script for ECEN5713 AESD Assignment 1
#Author: Justin Denning

#Verify we got both of the required arguments
if [ $# -lt 2 ]; then 
	echo "Script usage: writer.sh filename writestr"
	echo -e "\tfilename - the full path of a file to write the string to"
	echo -e "\twritestr - the string to write to the provided file"
	echo -e "\nThe writestr will be writtn to the filename. The file is overwritten if it exists, and the path will be created even if it doesn't already exist"
	exit 1
fi

#Ensure we were given a full file path
if [[ "$1" != /* ]]; then
	echo "filename must be a full path (meaning it must start with '/')"
	exit 1
fi

#Ensure the directory exists, or create it
dirname=`dirname $1`
if [ ! -d "$dirname" ]; then
	if ! mkdir -p "$dirname"; then
		echo "Error creating directory $dirname"
		exit 1
	fi
fi

#Output the provided string to the provided file
if ! echo "$2" > "$1"; then
	echo "Error writing string to file $1"
	exit 1
fi
exit 0

