#!/bin/sh

N_ARGS=2
if [[ $# -ne "$N_ARGS" ]]; then
	echo "The script requires 2 arguments:"
	echo "1. File directory"
	echo "2. Search string"
	echo "Usage : $0 <arg1> <arg2>"
	exit 1
fi

DIR=$1
STR=$2

if [[ ! -d "$DIR" ]]; then
	echo "$DIR is not a valid directory"
	exit 1
fi

N_FILES=$(find "$DIR" -type f | wc -l)

N_MATCHES=$(grep -R -I -F -o -- "$STR" "$DIR" | wc -l)

echo "The number of files are $N_FILES and the number of matching lines are $N_MATCHES"
