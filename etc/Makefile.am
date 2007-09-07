NULL =

confdir = $(PK_CONF_DIR)
conf_in_files = PackageKit.conf.in
conf_DATA = $(conf_in_files:.conf.in=.conf)

$(conf_DATA): $(conf_in_files) Makefile
	@sed -e "s|\@defaultbackend\@|$(DEFAULT_BACKEND)|" $< > $@

EXTRA_DIST =						\
	$(conf_in_files)				\
	$(NULL)

DISTCLEANFILES =					\
	PackageKit.conf

