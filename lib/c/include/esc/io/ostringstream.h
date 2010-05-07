/**
 * $Id: ostringstream.h 641 2010-05-02 18:24:30Z nasmussen $
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

#ifndef OSTRINGSTREAM_H_
#define OSTRINGSTREAM_H_

#include <esc/common.h>
#include <stdarg.h>
#include "outputstream.h"

/**
 * Opens a output-string-stream for the given string
 *
 * @param buffer the string
 * @param size the size of the buffer
 * @return the stream
 */
sOStream *osstream_open(char *buffer,s32 size);

#endif /* OSTRINGSTREAM_H_ */
