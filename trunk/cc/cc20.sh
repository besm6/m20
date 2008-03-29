#!/bin/sh
exec gcc -B/usr/local/lib/m20 -nostdinc -I/usr/local/lib/m20/include -S $*
