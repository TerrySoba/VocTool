#!/bin/bash

# make sure there is exactly one argument
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <input_file>"
    exit 1
fi

# create temporary file in /tmp
tmpfile=$(mktemp /tmp/playvoc.XXXXXX)

# install handler to remove temporary file on exit
trap "rm -f $tmpfile" EXIT

build/voctool -i $1 -o $tmpfile
mplayer $tmpfile
