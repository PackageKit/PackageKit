%.moc : %.h
	$(MOC) -i -o "$@" "$<"

clean-moc-extra:
	rm -vf *.moc

clean-am: clean-moc-extra

