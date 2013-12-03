NULL =

confdir = $(sysconfdir)/PackageKit
conf_in_files = PackageKit.conf.in
conf_DATA = $(conf_in_files:.conf.in=.conf)

$(conf_DATA): $(conf_in_files) Makefile
	@sed -e "s|\@defaultbackend\@|$(DEFAULT_BACKEND)|" $< > $@

vendordir = $(sysconfdir)/PackageKit
vendor_DATA = Vendor.conf

eventspostransdir = $(sysconfdir)/PackageKit/events/post-transaction.d
eventspostrans_DATA = README

eventspretransdir = $(sysconfdir)/PackageKit/events/pre-transaction.d
eventspretrans_DATA = README

EXTRA_DIST =						\
	$(conf_in_files)				\
	$(vendor_DATA)					\
	$(eventspostrans_DATA)				\
	$(NULL)

DISTCLEANFILES =					\
	PackageKit.conf


-include $(top_srcdir)/git.mk
