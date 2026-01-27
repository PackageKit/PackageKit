#!/bin/sh

find ${MESON_SOURCE_ROOT}/po \
	-type f \
	-iname "*.po" \
        -printf '%f\n' \
	| grep -oP '.*(?=[.])' | sort \
	> ${MESON_SOURCE_ROOT}/po/LINGUAS
