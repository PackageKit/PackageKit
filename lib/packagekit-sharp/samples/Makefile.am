all:			\
	listupdates	\
	ListUpdates.exe	\
	updatesystem	\
	UpdateSystem.exe

ListUpdates.exe: $(srcdir)/ListUpdates.cs ../packagekit-sharp.dll
	$(MCS) -out:$@ -r:$(srcdir)/../packagekit-sharp.dll $(GLIBSHARP_LIBS) ListUpdates.cs

listupdates: $(srcdir)/launcher.in Makefile
	sed -e 's,[@]sample[@],ListUpdates.exe,g' $(do_subst) < $(srcdir)/launcher.in > $@
	chmod +x $@

UpdateSystem.exe: $(srcdir)/UpdateSystem.cs ../packagekit-sharp.dll
	$(MCS) -debug -out:$@ -r:$(srcdir)/../packagekit-sharp.dll $(GLIBSHARP_LIBS) UpdateSystem.cs

updatesystem: $(srcdir)/launcher.in Makefile
	sed -e 's,[@]sample[@],UpdateSystem.exe,g' $(do_subst) < $(srcdir)/launcher.in > $@
	chmod +x $@



