/**
 * $Id$
 * Copyright (C) 2008 - 2009 Nils Asmussen
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

#include <esc/common.h>
#include <esc/heap.h>
#include <exceptions/io.h>
#include <string.h>

static const char *ex_printIOException(sIOException *e);

sIOException *ex_createIOException(s32 id,s32 line,const char *file,s32 errorNo) {
	sIOException *e = (sIOException*)malloc(sizeof(sIOException));
	if(!e)
		error("Unable to create exception-object (%s:%d)",file,line);
	e->handled = 0;
	e->id = id;
	e->line = line;
	e->file = file;
	e->error = errorNo;
	e->toString = (fToString)ex_printIOException;
	return e;
}

static const char *ex_printIOException(sIOException *e) {
	return strerror(e->error);
}
