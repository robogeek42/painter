#!/bin/bash 
if [ $# -lt 1 ]
then
	echo "usage: $0 <install directory>"
	echo "e.g. ./install.sh $HOME/sdcard/games/painter"
	exit -1
fi
DIR=$1

echo "Installing to $DIR"
if [ ! -d $DIR ]
then
	mkdir -p $DIR
fi

echo "copy assets"
rsync -rvu --progress img/ $DIR/img
rsync -rvu --progress levels/ $DIR/levels

echo "copy binary"
cp bin/painter.bin $DIR/painter

cat << EOF
done.

TO RUN painter : 

cd $DIR 
load painter
run
EOF
