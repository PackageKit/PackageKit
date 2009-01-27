all:			\
	ListUpdates.sh	\
	ListUpdates.exe	\
	UpdateSystem.sh	\
	UpdateSystem.exe

.cs.exe: ../packagekit-sharp.dll
	$(MCS) -debug -out:$@ -r:$(srcdir)/../packagekit-sharp.dll $(GLIBSHARP_LIBS) $<

.exe.sh: $(srcdir)/launcher.in
	sed -e 's,[@]sample[@],$<,g' $(do_subst) < $(srcdir)/launcher.in > $@
	chmod +x $@

CLEANFILES =		\
	*.exe		\
	*.exe.mdb	\
	*.sh
