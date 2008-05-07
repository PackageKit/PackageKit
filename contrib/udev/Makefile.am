udevrulesdir = ${SYSCONFDIR}/udev/rules.d
dist_udevrules_DATA = 51-packagekit-firmware.rules

install-data-hook:
	if test -w $(DESTDIR)/; then \
		mkdir -p $(DESTDIR)/lib/udev; \
		cp $(top_srcdir)/contrib/udev/packagekit-firmware.sh $(DESTDIR)/lib/udev; \
	fi

EXTRA_DIST = packagekit-firmware.sh

