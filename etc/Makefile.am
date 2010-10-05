NULL =

confdir = $(PK_CONF_DIR)
conf_in_files = PackageKit.conf.in
conf_DATA = $(conf_in_files:.conf.in=.conf)

$(conf_DATA): $(conf_in_files) Makefile
	@sed -e "s|\@defaultbackend\@|$(DEFAULT_BACKEND)|" $< > $@

vendordir = $(PK_CONF_DIR)
vendor_DATA = Vendor.conf

eventspostransdir = $(PK_CONF_DIR)/events/post-transaction.d
eventspostrans_DATA = README

eventspretransdir = $(PK_CONF_DIR)/events/pre-transaction.d
eventspretrans_DATA = README

EXTRA_DIST =						\
	$(conf_in_files)				\
	$(vendor_DATA)					\
	$(eventspostrans_DATA)				\
	$(NULL)

DISTCLEANFILES =					\
	PackageKit.conf

