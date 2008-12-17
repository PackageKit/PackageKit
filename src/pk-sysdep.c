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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "pk-sysdep.h"
#ifdef linux

static inline int 
ioprio_set(int which, int who, int ioprio)
{
	return syscall (SYS_ioprio_set, which, who, ioprio);
}

int 
pk_set_ioprio_idle(pid_t pid) 
{
	int prio = 7;
	int class = IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT;

	return ioprio_set (IOPRIO_WHO_PROCESS, pid, prio | class);
}

#endif
