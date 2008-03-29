SUBDIRS = . as cc

clean-local:
	-rm -rf *~

distclean-local:
	-rm -rf autom4te.cache

log:	.svn
	svn update
	svn log > ChangeLog
