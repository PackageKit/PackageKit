%.moc : %.h $(top_srcdir)/.moc.version
	$(MOC) -i -o "$@" "$<"

$(top_srcdir)/.moc.version : $(MOC)
	# "moc -v" always fails :-/
	$(MOC) -v > $@.tmp 2>&1 || :
	if test ! -f "$@" || test "`cat $@`" != "`cat $@.tmp`"; then \
		mv -f $@.tmp $@; \
	else \
		rm -f $@.tmp; \
	fi

clean-moc-extra:
	rm -vf *.moc
	rm -vf $(top_srcdir)/.moc.version

clean-am: clean-moc-extra

