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

#include <esc/col/slist.h>
#include <vfs/node.h>
#include <vfs/openfile.h>
#include <common.h>
#include <utime.h>

class Proc;
class VFSChannel;

class VFSFS {
	VFSFS() = delete;

public:
	/**
	 * Retrieves information about the file, denoted by <chan>.
	 *
	 * @param chan the channel for the file to the fs instance
	 * @param info should be filled
	 * @return 0 on success
	 */
	static int fstat(VFSChannel *chan,struct stat *info);

	/**
	 * Truncates the file, denoted by <chan>, to <length> bytes.
	 *
	 * @param chan the channel for the file to the fs instance
	 * @param length the desired length
	 * @return 0 on success
	 */
	static int truncate(VFSChannel *chan,off_t length);

	/**
	 * Writes all cached blocks of the affected filesystem to disk.
	 *
	 * @param chan the channel for the file to the fs instance
	 * @return 0 on success
	 */
	static int syncfs(VFSChannel *chan);

	/**
	 * Changes the permissions of the file denoted by <chan>
	 *
	 * @param chan the channel for the file to the fs instance
	 * @param mode the new mode
	 * @return 0 on success
	 */
	static int chmod(VFSChannel *chan,mode_t mode);

	/**
	 * Changes the owner and group of the file denoted by <chan>.
	 *
	 * @param chan the channel for the file to the fs instance
	 * @param uid the user-id
	 * @param gid the group-id
	 * @return 0 on success
	 */
	static int chown(VFSChannel *chan,uid_t uid,gid_t gid);

	/**
	 * Sets the access and modification times of the file denoted by <chan>
	 *
	 * @param chan the channel for the file to the fs instance
	 * @param utimes the new access and modification times (NULL = set to current time)
	 * @return 0 on success
	 */
	static int utime(VFSChannel *chan,const struct utimbuf *utimes);

	/**
	 * Creates a hardlink at <dir>/<name> which points to <target>
	 *
	 * @param target the channel for the target file
	 * @param dir the channel for the directory
	 * @param name the name of the link
	 * @return 0 on success
	 */
	static int link(VFSChannel *target,VFSChannel *dir,const char *name);

	/**
	 * Unlinks the file, denoted by <chan>/<name>. That means, the directory-entry will be removed
	 * and if there are no more references to the inode, it will be removed.
	 *
	 * @param chan the channel for the directory
	 * @param name the filename to remove
	 * @return 0 on success
	 */
	static int unlink(VFSChannel *chan,const char *name);

	/**
	 * Renames <old> to <dir>/<name>
	 *
	 * @param old the channel for the old file
	 * @param dir the channel for the directory of the new file
	 * @param name the name of the new file
	 * @return 0 on success
	 */
	static int rename(VFSChannel *oldDir,const char *oldName,VFSChannel *newDir,
		const char *newName);

	/**
	 * Creates a directory named <name> in the directory denoted by <chan>.
	 *
	 * @param chan the channel for the file to the fs instance
	 * @param name the directory name
	 * @param mode the mode to set
	 * @return 0 on success
	 */
	static int mkdir(VFSChannel *chan,const char *name,mode_t mode);

	/**
	 * Removes the directory named <name> in the directory denoted by <chan>.
	 *
	 * @param chan the channel for the file to the fs instance
	 * @param name the directory name
	 * @return 0 on success
	 */
	static int rmdir(VFSChannel *chan,const char *name);

	/**
	 * Creates a symlink named <name> in the directory denoted by <chan>, pointing to <target>.
	 *
	 * @param chan the channel for the file to the fs instance
	 * @param name the filename
	 * @param target the target path
	 * @return 0 on success
	 */
	static int symlink(VFSChannel *chan,const char *name,const char *target);
};
