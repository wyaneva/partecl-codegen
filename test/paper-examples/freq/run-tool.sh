#!/bin/bash
#run ParTeCL and copy the tests to the output dir

#DIR=$1
#SOURCE=$2
#CONFIG=$3
#ADDITIONAL=${4:-none}

DIR="."
SOURCE="freq.c"
CONFIG="freq.config"
#ADDITIONAL=""

DIRSOURCE="$DIR/$SOURCE"
DIRCONFIG="$DIR/$CONFIG"
DIRTESTS="$DIR/tests.txt"
#DIRADDITIONAL="$DIR/$ADDITIONAL"

#clear the destination folder
rm -r /home/vanya/partecl-runtime/kernel-gen/
mkdir /home/vanya/partecl-runtime/kernel-gen/

#run partecl-codegen 
~/clang-llvm/build/bin/partecl-codegen $DIRSOURCE -config $DIRCONFIG -output ~/partecl-runtime/kernel-gen --

#copy the tests
cp $DIRTESTS /home/vanya/partecl-runtime/kernel-gen/

#copy header files
#cp -r $DIR/*.h /home/vanya/partecl-runtime/kernel-gen/
#cp -r headers/* /home/vanya/partecl-runtime/kernel-gen/

#copy additional files
#if [ $ADDITIONAL = "-none" ]
#then
#  cp -r $DIRADDITIONAL /home/vanya/partecl-runtime/kernel-gen/
#fi
