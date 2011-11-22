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

#ifndef I586_THREAD_H_
#define I586_THREAD_H_

#include <sys/common.h>

static A_INLINE sThread *thread_getRunning(void) {
	uint32_t esp;
	__asm__ (
		"mov	%%esp,%0;"
		/* outputs */
		: "=r" (esp)
		/* inputs */
		:
	);
	uint32_t* tptr = (uint32_t*)((esp & 0xFFFFF000) + 0xFFC);
	return *tptr;
}

static A_INLINE void thread_setRunning(sThread *t) {
	uint32_t esp;
	__asm__ (
		"mov	%%esp,%0;"
		/* outputs */
		: "=r" (esp)
		/* inputs */
		:
	);
	sThread** tptr = (sThread**)((esp & 0xFFFFF000) + 0xFFC);
	*tptr = t;
}

/**
 * Performs the initial switch for APs, because these don't run a thread yet.
 */
void thread_initialSwitch(void);

/**
 * Performs a thread-switch
 */
void thread_doSwitch(void);

#endif /* I586_THREAD_H_ */
