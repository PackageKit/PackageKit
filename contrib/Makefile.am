SUBDIRS = udev

if BACKEND_TYPE_YUM
SUBDIRS += yum-packagekit
endif

bashcompletiondir = ${SYSCONFDIR}/bash_completion.d
dist_bashcompletion_DATA = pk-completion.bash

script_SCRIPTS =					\
	packagekit-bugreport.sh

EXTRA_DIST=$(script_SCRIPTS)

scriptdir = $(bindir)

