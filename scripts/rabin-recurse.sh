#!/bin/sh

cmd="rabin"
levels="-l 3"

usage() {
    echo >&2 "Usage: $0 -s stats-dir [-c rabin-cmd] [-l stats-levels] [-n notation] [-b bits] [-m min-chunk] [-M max-chunk] index-dir ..."
}

while getopts c:s:l:n:b:m:M:f: o ;do
	case "$o" in
	c)	cmd="$OPTARG";;
	s)	statsDir="$OPTARG";;
	l)	levels="-l $OPTARG";;
	n)	notation="-n $OPTARG";;
	b)	bits="-b $OPTARG";;
	m)	min="-m $OPTARG";;
	M)	max="-M $OPTARG";;
	f)	fixed="-f $OPTARG";;
	[?])	usage
		exit 1;;
	esac
done
shift `expr $OPTIND - 1`

if [ ! -x "$cmd" -o ! -f "$cmd" ] ;then
    echo "error: $cmd does not seem to be a valid executable"
    usage
    exit 1
fi

if [ -z "$statsDir" ] ;then
    echo >&2 "error: stats-dir must be specified; use the -s option"
    usage
    exit 1;
fi

if [ $# -eq 0 ] ;then
    echo >&2 "error: one or more index-dirs must be specified"
    usage
    exit 1;
fi

for d in "$@" ;do
    if [ ! -d "$d" ] ;then
	echo >&2 "error: $d is not a directory; skipping"
	continue
    fi
    find "$d" -type f \
         -exec $cmd -s $statsDir $levels $notation $bits $min $max \
               $fixed {} \;
done
