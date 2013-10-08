/**
 * $Id$
 * Copyright (C) 2008 - 2011 Nils Asmussen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include <esc/common.h>
#include <esc/thread.h>

static inline int crtlocku(tULock *l) {
	l->value = 1;
	l->sem = semcreate(0);
	return l->sem;
}

static inline void locku(tULock *l) {
	/* 1 means free, <= 0 means taken */
	if(__sync_fetch_and_add(&l->value, -1) <= 0)
		semdown(l->sem);
}

static inline void unlocku(tULock *l) {
	if(__sync_fetch_and_add(&l->value, +1) < 0)
		semup(l->sem);
}

static inline void remlocku(tULock *l) {
	semdestroy(l->sem);
}
