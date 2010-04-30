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

#ifndef ISTRINGSTREAM_H_
#define ISTRINGSTREAM_H_

#include <esc/common.h>
#include "inputstream.h"

typedef struct {
	s32 pos;
	s32 length;
	const char *buffer;
} sISStream;

sIStream *isstream_open(const char *str);
s32 isstream_seek(sIStream *s,s32 offset,u32 whence);
bool isstream_eof(sIStream *s);
void isstream_close(sIStream *s);

#endif /* ISTRINGSTREAM_H_ */
