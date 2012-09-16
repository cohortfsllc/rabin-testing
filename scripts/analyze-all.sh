#!/bin/sh

for d in "$@" ; do
    d2=`basename $d`
    analyze.sh $d >${d2}-analyze.out
done
