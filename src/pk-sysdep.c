/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Adel Gadllah <adel.gadllah@gmail.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <glib.h>
#include <unistd.h>

#ifdef linux
 #include <sys/syscall.h>
#endif

#include "pk-sysdep.h"

#ifdef linux

enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE
};

enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER
};

#define IOPRIO_CLASS_SHIFT	13

/**
 * ioprio_set:
 *
 * FIXME: glibc should have this function
 **/
static inline gint
ioprio_set (gint which, gint who, gint ioprio)
{
	return syscall (SYS_ioprio_set, which, who, ioprio);
}

/**
 * pk_ioprio_set_idle:
 *
 * Set the IO priority to idle
 **/
gboolean
pk_ioprio_set_idle (GPid pid)
{
	gint prio = 7;
	gint class = IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT;

	return (ioprio_set (IOPRIO_WHO_PROCESS, pid, prio | class) == 0);
}

#else

/**
 * pk_ioprio_set_idle:
 **/
gboolean
pk_ioprio_set_idle (GPid pid)
{
	return TRUE;
}

#endif
