SUBDIRS = img videos

HTML_FILES =				\
	style.css			\
	index.html			\
	pk-authors.html			\
	pk-bugs.html			\
	pk-download.html		\
	pk-faq.html			\
	pk-help.html			\
	pk-intro.html			\
	pk-matrix.html			\
	pk-profiles.html		\
	pk-reference.html		\
	pk-screenshots.html		\
	pk-package-not-found.html	\
	pk-using.html

helpfiledir = $(datadir)/PackageKit/website
helpfile_DATA = $(HTML_FILES)

EXTRA_DIST = $(HTML_FILES)

clean-local:
	rm -f *~


-include $(top_srcdir)/git.mk
