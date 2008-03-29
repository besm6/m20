bin_PROGRAMS = cc1
cc1_SOURCES = parser.y scanner.l compiler.c machdep.c
cc1_LDADD = -ll

AM_CFLAGS = -Wall -g -O
AM_YFLAGS = -d

clean-local:
	-rm -rf *~ parser.c parser.h scanner.c

distclean-local:
	-rm -rf autom4te.cache

install: ${libdir}/m20/cc1 ${bindir}/cc20

${libdir}/m20/cc1: cc1
	${install_sh_PROGRAM} $< $@

${bindir}/cc20: cc20.sh
	${install_sh_PROGRAM} $< $@
