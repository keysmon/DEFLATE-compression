#!/bin/bash

if [ -z "$BASH_VERSION" ]
then
    echo 'This script requires bash (try something like "bash validate_on_test_data.sh")'
    exit 1
fi

make

PASSED=0
FAILED=0
for filename in `find ./test_data -type f`
do
    echo Checking $filename
    ./uvgz < $filename > validate_temp.bin
    gzip -d < validate_temp.bin > validate_output_temp.txt
    diff -qs $filename validate_output_temp.txt > /dev/null

    if [ "$?" -ne "0" ]
    then 
        echo FAILED
        FAILED=$[ $FAILED + 1 ]
    else
        OLDSIZE=`wc $filename | awk '{print $3}'`
        NEWSIZE=`wc validate_temp.bin | awk '{print $3}'`
        echo Passed: $NEWSIZE bytes \($[100*$OLDSIZE/$NEWSIZE ]%\)
        PASSED=$[ $PASSED + 1 ]
    fi

    rm validate_output_temp.txt validate_temp.bin
done

echo ${PASSED}/$[ $PASSED + $FAILED ] passed, ${FAILED}/$[ $PASSED + $FAILED ] failed
