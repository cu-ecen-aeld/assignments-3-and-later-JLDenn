#!/bin/bash
# finder script for ECEN5713 AESD Assignment 1
#Author: Justin Denning

set -e
set -u

#Verify we got both of the required arguments
if [ $# -lt 2 ]; then 
	echo "Script usage: finder.sh filesdir searchstr"
	echo -e "\tfilesdir - the directory to search for files in"
	echo -e "\tsearchstr - the string to find"
	echo -e "\nThis script will first count all the files in filesdir (including subdirectories), then it will search the contents of those files for searchstr and return the number of matching lines"
	exit 1
fi

#Check to see if the provided directory is actually a valid directory
if [ ! -d "$1" ]; then
	echo "Directory doesn't exist: $1"
	exit 1
fi

#Get the total number of files in the provided directory and sub-directories
numfiles=`find "$1" -type f | wc -l`

#Search each of the files for the provided search string, then count the matching lines
numlines=`grep -r "$2" "$1" | wc -l`
echo "The number of files are $numfiles and the number of matching lines are $numlines"
exit 0