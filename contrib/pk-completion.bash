#
# bash completion support for PackageKit's console commands.
#
# Copyright (C) 2007 James Bowes <jbowes@dangerouslyinc.com>
# Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# To use the completion:
#   1. Copy this file somewhere (e.g. ~/.pk-completion.sh).
#   2. Add the following line to your .bashrc:
#        source ~/.git-completion.sh


__pkcon_commandlist="
    accept-eula
    get-roles
    get-depends
    get-details
    get-distro-upgrades
    get-files
    get-filters
    get-groups
    get-packages
    download
    get-requires
    get-time
    get-transactions
    get-update-detail
    get-updates
    get-categories
    install
    install-local
    list-create
    refresh
    remove
    repo-disable
    repo-enable
    repo-list
    repo-set-data
    resolve
    search
    update
    upgrade-system
    repair
    "

__pkconcomp ()
{
	local all c s=$'\n' IFS=' '$'\t'$'\n'
	local cur="${COMP_WORDS[COMP_CWORD]}"
	if [ $# -gt 2 ]; then
		cur="$3"
	fi
	for c in $1; do
		case "$c$4" in
		*.)    all="$all$c$4$s" ;;
		*)     all="$all$c$4 $s" ;;
		esac
	done
	IFS=$s
	COMPREPLY=($(compgen -P "$2" -W "$all" -- "$cur"))
	return
}

_pkcon_search ()
{
	local i c=1 command
	while [ $c -lt $COMP_CWORD ]; do
		i="${COMP_WORDS[c]}"
		case "$i" in
            name|details|group|file)
			command="$i"
			break
			;;
		esac
		c=$((++c))
	done

	if [ $c -eq $COMP_CWORD -a -z "$command" ]; then
        __pkconcomp "name details group file"
    fi
    return
}

_pkcon ()
{
	local i c=1 command

	while [ $c -lt $COMP_CWORD ]; do
		i="${COMP_WORDS[c]}"
		case "$i" in
		--version|--help|--verbose|--nowait|-v|-n|-h|-?) ;;
		*) command="$i"; break ;;
		esac
		c=$((++c))
	done

    if [ $c -eq $COMP_CWORD -a -z "$command" ]; then
		case "${COMP_WORDS[COMP_CWORD]}" in
		--*=*) COMPREPLY=() ;;
		--*)   __pkconcomp "
			--version
			--filter
			--verbose
            --help
            --nowait
			"
			;;
        -*) __pkconcomp "
            -v
            -n
            -h
            -?
			--version
			--verbose
            --help
            --filter
            --nowait
            "
            ;;
		*)     __pkconcomp "$__pkcon_commandlist" ;;
		esac
		return
	fi

	case "$command" in
	search)      _pkcon_search ;;
	*)           COMPREPLY=() ;;
	esac
}

complete -o default -o nospace -F _pkcon pkcon
