#!/bin/sh

THISDIR=`dirname $0`
DYLD_INSERT_LIBRARIES=$THISDIR/library-override.dylib exec $*
