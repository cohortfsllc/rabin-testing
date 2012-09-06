#!/bin/sh

if [ $# -ne 1 ] ;then
    echo Usage: $0 stats-dir
    exit 1
fi

fifo=/tmp/`basename $0`-fifo.$$

dir=$1

actualSize=0
dedupSize=0
duplicateCount=0

processHash() {
    count=`find $1 -name "*.stats" -print | wc -l`
    size=`cat ${1}/*.size`

    dedupSize=`expr $dedupSize + $size`
    actualSize=`expr $actualSize + $count \* $size`
    duplicateCount=`expr $duplicateCount + $count - 1`
}

# we're using a fifo because when trying to pipe output of find to
# while loop ran into some scoping issues

mkfifo $fifo

find $dir -name "*.hash" -print > $fifo &
while read f ;do
    processHash $f
done < $fifo

echo "Duplicate Blocks Found :" $duplicateCount
echo "De-duplicated Size     :" $dedupSize
echo "Expanded Size          :" $actualSize
echo "Savings                :" `expr \( $actualSize - $dedupSize \) \* 100 / $actualSize` "%"

rm $fifo
