#!/bin/bash

N_ARGS=2

if [[ $# -ne "$N_ARGS" ]]; then
	echo "This script requires 2 arguments:"
	echo "1. Full path to a file"
	echo "2. Text string to be written"
	echo "Usage : $0 <arg1> <arg2>"
	exit 1
fi

FILE=$1
STR=$2

mkdir -p "$(dirname "$FILE")" || { echo "Failed to create directory"; exit 1; }

echo "$STR" > "$FILE" || { echo "Failed to create or truncate file"; exit 1; }
