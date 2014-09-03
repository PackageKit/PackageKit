confdir = $(sysconfdir)/PackageKit
conf_DATA = PackageKit.conf Vendor.conf

eventspostransdir = $(sysconfdir)/PackageKit/events/post-transaction.d
eventspostrans_DATA = README

eventspretransdir = $(sysconfdir)/PackageKit/events/pre-transaction.d
eventspretrans_DATA = README

EXTRA_DIST =						\
	$(conf_DATA)					\
	$(eventspostrans_DATA)

-include $(top_srcdir)/git.mk
