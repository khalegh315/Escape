/**
 * $Id$
 * Copyright (C) 2008 - 2014 Nils Asmussen
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

#include <vfs/node.h>
#include <common.h>

class VFSDir : public VFSNode {
	/* VFS-directory-entry (equal to the direntry of ext2) */
	struct VFSDirEntry {
		ino_t nodeNo;
		uint16_t recLen;
		uint16_t nameLen;
		/* name follows (up to 255 bytes) */
	} A_PACKED;

public:
	/**
	 * Creates a new directory with name <name> in <parent>
	 *
	 * @param u the user
	 * @param parent the parent-node
	 * @param name the name
	 * @param mode the mode to set
	 * @param success whether the constructor succeeded (is expected to be true before the call!)
	 */
	explicit VFSDir(const fs::User &u,VFSNode *parent,char *name,uint mode,bool &success);

	virtual off_t seek(off_t position,off_t offset,uint whence) const override;
	virtual ssize_t getSize() override;
	virtual ssize_t read(OpenFile *file,USER void *buffer,off_t offset,size_t count) override;

private:
	static void add(VFSDirEntry *&dirEntry,ino_t ino,const char *name,size_t len);
};
