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

#include <sys/common.h>
#include <sys/task/proc.h>
#include <sys/mem/cache.h>
#include <sys/vfs/openfile.h>
#include <sys/vfs/fs.h>
#include <sys/vfs/channel.h>
#include <sys/vfs/device.h>
#include <sys/vfs/sem.h>
#include <sys/vfs/vfs.h>
#include <sys/ostream.h>
#include <esc/messages.h>
#include <errno.h>

#define FILE_COUNT					(gftArray.getObjCount())

klock_t OpenFile::gftLock;
DynArray OpenFile::gftArray(sizeof(OpenFile),GFT_AREA,GFT_AREA_SIZE);
OpenFile *OpenFile::gftFreeList = NULL;
extern klock_t waitLock;

void OpenFile::decUsages() {
	SpinLock::acquire(&lock);
	assert(usageCount > 0);
	usageCount--;
	/* if it should be closed in the meanwhile, we have to close it now, because it wasn't possible
	 * previously because of our usage */
	if(EXPECT_FALSE(usageCount == 0 && refCount == 0))
		doClose(Proc::getRunning());
	SpinLock::release(&lock);
}

int OpenFile::fcntl(A_UNUSED pid_t pid,uint cmd,int arg) {
	switch(cmd) {
		case F_GETACCESS:
			return flags & (VFS_READ | VFS_WRITE | VFS_MSGS);
		case F_GETFL:
			return flags & VFS_NOBLOCK;
		case F_SETFL:
			SpinLock::acquire(&lock);
			flags &= VFS_READ | VFS_WRITE | VFS_MSGS | VFS_CREATE | VFS_DEVICE;
			flags |= arg & VFS_NOBLOCK;
			SpinLock::release(&lock);
			return 0;
		case F_SETDATA: {
			VFSNode *n = node;
			int res = 0;
			SpinLock::acquire(&waitLock);
			if(EXPECT_FALSE(devNo != VFS_DEV_NO || !IS_DEVICE(n->getMode())))
				res = -EINVAL;
			else
				res = static_cast<VFSDevice*>(n)->setReadable((bool)arg);
			SpinLock::release(&waitLock);
			return res;
		}
		case F_SETUNUSED: {
			VFSNode *n = node;
			int res = 0;
			SpinLock::acquire(&waitLock);
			if(EXPECT_FALSE(devNo != VFS_DEV_NO || !IS_CHANNEL(n->getMode())))
				res = -EINVAL;
			else
				static_cast<VFSChannel*>(n)->setUsed(false);
			SpinLock::release(&waitLock);
			return res;
		}
		case F_SEMUP:
		case F_SEMDOWN: {
			if(EXPECT_FALSE(devNo != VFS_DEV_NO || !IS_SEM(node->getMode())))
				return -EINVAL;
			if(EXPECT_FALSE(~flags & VFS_SEM))
				return -EPERM;
			if(cmd == F_SEMUP)
				static_cast<VFSSem*>(node)->up();
			else
				static_cast<VFSSem*>(node)->down();
			return 0;
		}
	}
	return -EINVAL;
}

int OpenFile::fstat(pid_t pid,USER sFileInfo *info) const {
	int res = 0;
	if(devNo == VFS_DEV_NO)
		node->getInfo(pid,info);
	else
		res = VFSFS::istat(pid,nodeNo,devNo,info);
	return res;
}

off_t OpenFile::seek(pid_t pid,off_t offset,uint whence) {
	off_t res;

	/* don't lock it during VFSFS::istat(). we don't need it in this case because position is
	 * simply set and never restored to oldPos */
	if(devNo == VFS_DEV_NO || whence != SEEK_END)
		SpinLock::acquire(&lock);

	off_t oldPos = position;
	if(devNo == VFS_DEV_NO) {
		res = node->seek(pid,position,offset,whence);
		if(EXPECT_FALSE(res < 0)) {
			SpinLock::release(&lock);
			return res;
		}
		position = res;
	}
	else {
		if(whence == SEEK_END) {
			sFileInfo info;
			res = VFSFS::istat(pid,nodeNo,devNo,&info);
			if(EXPECT_FALSE(res < 0))
				return res;
			/* can't be < 0, therefore it will always be kept */
			position = info.size;
		}
		/* since the fs-device validates the position anyway we can simply set it */
		else if(whence == SEEK_SET)
			position = offset;
		else
			position += offset;
	}

	/* invalid position? */
	if(EXPECT_FALSE(position < 0)) {
		position = oldPos;
		res = -EINVAL;
	}
	else
		res = position;

	if(devNo == VFS_DEV_NO || whence != SEEK_END)
		SpinLock::release(&lock);
	return res;
}

ssize_t OpenFile::read(pid_t pid,USER void *buffer,size_t count) {
	if(EXPECT_FALSE(!(flags & VFS_READ)))
		return -EACCES;

	ssize_t readBytes;
	if(devNo == VFS_DEV_NO) {
		/* use the read-handler */
		readBytes = node->read(pid,this,buffer,position,count);
	}
	else {
		/* query the fs-device to read from the inode */
		readBytes = VFSFS::read(pid,nodeNo,devNo,buffer,position,count);
	}

	if(EXPECT_TRUE(readBytes > 0)) {
		SpinLock::acquire(&lock);
		position += readBytes;
		SpinLock::release(&lock);
	}

	if(EXPECT_TRUE(readBytes > 0 && pid != KERNEL_PID)) {
		Proc *p = Proc::getByPid(pid);
		/* no lock here because its not critical. we don't make decisions based on it or similar.
		 * its just for statistics. therefore, it doesn't really hurt if we add a bit less in
		 * very very rare cases. */
		p->getStats().input += readBytes;
	}
	return readBytes;
}

ssize_t OpenFile::write(pid_t pid,USER const void *buffer,size_t count) {
	if(EXPECT_FALSE(!(flags & VFS_WRITE)))
		return -EACCES;

	ssize_t writtenBytes;
	if(devNo == VFS_DEV_NO) {
		/* write to the node */
		writtenBytes = node->write(pid,this,buffer,position,count);
	}
	else {
		/* query the fs-device to write to the inode */
		writtenBytes = VFSFS::write(pid,nodeNo,devNo,buffer,position,count);
	}

	if(EXPECT_TRUE(writtenBytes > 0)) {
		SpinLock::acquire(&lock);
		position += writtenBytes;
		SpinLock::release(&lock);
	}

	if(EXPECT_TRUE(writtenBytes > 0 && pid != KERNEL_PID)) {
		Proc *p = Proc::getByPid(pid);
		/* no lock; same reason as above */
		p->getStats().output += writtenBytes;
	}
	return writtenBytes;
}

ssize_t OpenFile::sendMsg(pid_t pid,msgid_t id,USER const void *data1,size_t size1,
		USER const void *data2,size_t size2) {
	if(EXPECT_FALSE(devNo != VFS_DEV_NO))
		return -EPERM;
	/* the device-messages (open, read, write, close) are always allowed */
	if(EXPECT_FALSE(!IS_DEVICE_MSG(id) && !(flags & VFS_MSGS)))
		return -EACCES;

	if(EXPECT_FALSE(!IS_CHANNEL(node->getMode())))
		return -ENOTSUP;

	ssize_t err = static_cast<VFSChannel*>(node)->send(pid,flags,id,data1,size1,data2,size2);
	if(EXPECT_TRUE(err == 0 && pid != KERNEL_PID)) {
		Proc *p = Proc::getByPid(pid);
		/* no lock; same reason as above */
		p->getStats().output += size1 + size2;
	}
	return err;
}

ssize_t OpenFile::receiveMsg(pid_t pid,USER msgid_t *id,USER void *data,size_t size,
		bool forceBlock) {
	if(EXPECT_FALSE(devNo != VFS_DEV_NO))
		return -EPERM;

	if(EXPECT_FALSE(!IS_CHANNEL(node->getMode())))
		return -ENOTSUP;

	ssize_t err = static_cast<VFSChannel*>(node)->receive(pid,flags,id,data,size,
			forceBlock || !(flags & VFS_NOBLOCK),forceBlock);
	if(EXPECT_TRUE(err > 0 && pid != KERNEL_PID)) {
		Proc *p = Proc::getByPid(pid);
		/* no lock; same reason as above */
		p->getStats().input += err;
	}
	return err;
}

bool OpenFile::close(pid_t pid) {
	SpinLock::acquire(&lock);
	bool res = doClose(pid);
	SpinLock::release(&lock);
	return res;
}

bool OpenFile::doClose(pid_t pid) {
	/* decrement references; it may be already zero if we have closed the file previously but
	 * couldn't free it because there was still a user of it. */
	if(EXPECT_TRUE(refCount > 0))
		refCount--;

	/* if there are no more references, free the file */
	if(refCount == 0) {
		/* if we have used a file-descriptor to get here, the usages are at least 1; otherwise it is
		 * 0, because it is used kernel-intern only and not passed to other "users". */
		if(usageCount <= 1) {
			if(devNo == VFS_DEV_NO)
				node->close(pid,this);
			/* VFSFS::close won't cause a context-switch; therefore we can keep the lock */
			else
				VFSFS::close(pid,nodeNo,devNo);

			/* mark unused */
			Cache::free(path);
			flags = 0;
			releaseFile(this);
			return true;
		}
	}
	return false;
}

int OpenFile::getClient(OpenFile *file,int *fd,uint flags) {
	Thread *t = Thread::getRunning();
	if(EXPECT_FALSE(file->devNo != VFS_DEV_NO))
		return -EPERM;
	if(EXPECT_FALSE(!IS_DEVICE(file->node->getMode())))
		return -EPERM;

	while(true) {
		SpinLock::acquire(&waitLock);
		*fd = static_cast<VFSDevice*>(file->node)->getWork(flags);
		/* if we've found one or we shouldn't block, stop here */
		if(EXPECT_TRUE(*fd >= 0 || (flags & GW_NOBLOCK))) {
			SpinLock::release(&waitLock);
			return *fd >= 0 ? 0 : -ENOCLIENT;
		}

		/* wait for a client (accept signals) */
		t->wait(EV_CLIENT,(evobj_t)file->node);
		SpinLock::release(&waitLock);

		Thread::switchAway();
		if(EXPECT_FALSE(t->hasSignalQuick()))
			return -EINTR;
	}
	A_UNREACHED;
}

void OpenFile::print(OStream &os) const {
	os.writef("%3d [ %2u refs, %2u uses (%u:%u",
			gftArray.getIndex(this),refCount,usageCount,devNo,nodeNo);
	if(devNo == VFS_DEV_NO && VFSNode::isValid(nodeNo))
		os.writef(":%s)",node->getPath());
	else
		os.writef(")");
	os.writef(" ]");
}

void OpenFile::printAll(OStream &os) {
	os.writef("Global File Table:\n");
	for(size_t i = 0; i < FILE_COUNT; i++) {
		OpenFile *f = (OpenFile*)gftArray.getObj(i);
		if(f->flags != 0) {
			os.writef("\tfile @ index %d\n",i);
			os.writef("\t\tflags: ");
			if(f->flags & VFS_READ)
				os.writef("READ ");
			if(f->flags & VFS_WRITE)
				os.writef("WRITE ");
			if(f->flags & VFS_NOBLOCK)
				os.writef("NOBLOCK ");
			if(f->flags & VFS_DEVICE)
				os.writef("DEVICE ");
			if(f->flags & VFS_MSGS)
				os.writef("MSGS ");
			os.writef("\n");
			os.writef("\t\tnodeNo: %d\n",f->nodeNo);
			os.writef("\t\tdevNo: %d\n",f->devNo);
			os.writef("\t\tpos: %Od\n",f->position);
			os.writef("\t\trefCount: %d\n",f->refCount);
			if(f->owner == KERNEL_PID)
				os.writef("\t\towner: %d (kernel)\n",f->owner);
			else {
				const Proc *p = Proc::getByPid(f->owner);
				os.writef("\t\towner: %d:%s\n",f->owner,p ? p->getProgram() : "???");
			}
			if(f->devNo == VFS_DEV_NO) {
				VFSNode *n = f->node;
				if(!n->isAlive())
					os.writef("\t\tFile: <destroyed>\n");
				else
					os.writef("\t\tFile: '%s'\n",f->getPath());
			}
			else
				os.writef("\t\tFile: '%s'\n",f->getPath());
		}
	}
}

size_t OpenFile::getCount() {
	size_t count = 0;
	for(size_t i = 0; i < FILE_COUNT; i++) {
		OpenFile *f = (OpenFile*)gftArray.getObj(i);
		if(f->flags != 0)
			count++;
	}
	return count;
}

int OpenFile::getFree(pid_t pid,ushort flags,inode_t nodeNo,dev_t devNo,const VFSNode *n,OpenFile **f) {
	const uint userFlags = VFS_READ | VFS_WRITE | VFS_MSGS | VFS_NOBLOCK | VFS_DEVICE | VFS_EXCLUSIVE;
	size_t i;
	bool isDrvUse = false;
	OpenFile *e;
	/* ensure that we don't increment usages of an unused slot */
	assert(flags & (VFS_READ | VFS_WRITE | VFS_MSGS));
	assert(!(flags & ~userFlags));

	if(devNo == VFS_DEV_NO) {
		/* we can add pipes here, too, since every open() to a pipe will get a new node anyway */
		isDrvUse = (n->getMode() & (MODE_TYPE_CHANNEL | MODE_TYPE_PIPE)) ? true : false;
	}
	/* doesn't work for channels and pipes */
	if(EXPECT_FALSE(isDrvUse && (flags & VFS_EXCLUSIVE)))
		return -EINVAL;

	SpinLock::acquire(&gftLock);
	/* for devices it doesn't matter whether we use an existing file or a new one, because it is
	 * no problem when multiple threads use it for writing */
	if(!isDrvUse) {
		/* TODO walk through used-list and pick first from freelist */
		ushort rwFlags = flags & userFlags;
		for(i = 0; i < FILE_COUNT; i++) {
			e = (OpenFile*)gftArray.getObj(i);
			/* used slot and same node? */
			if(e->flags != 0) {
				/* same file? */
				if(e->devNo == devNo && e->nodeNo == nodeNo) {
					if(e->owner == pid) {
						/* if the flags are the same we don't need a new file */
						if((e->flags & userFlags) == rwFlags) {
							e->incRefs();
							SpinLock::release(&gftLock);
							*f = e;
							return 0;
						}
					}
					else if((rwFlags & VFS_EXCLUSIVE) || (e->flags & VFS_EXCLUSIVE)) {
						SpinLock::release(&gftLock);
						return -EBUSY;
					}
				}
			}
		}
	}

	/* if there is no free slot anymore, extend our dyn-array */
	if(EXPECT_FALSE(gftFreeList == NULL)) {
		size_t j;
		i = gftArray.getObjCount();
		if(!gftArray.extend()) {
			SpinLock::release(&gftLock);
			return -ENFILE;
		}
		/* put all except i on the freelist */
		for(j = i + 1; j < gftArray.getObjCount(); j++) {
			e = (OpenFile*)gftArray.getObj(j);
			e->next = gftFreeList;
			gftFreeList = e;
		}
		*f = e = (OpenFile*)gftArray.getObj(i);
	}
	else {
		/* use the first from the freelist */
		e = gftFreeList;
		gftFreeList = gftFreeList->next;
		*f = e;
	}
	SpinLock::release(&gftLock);

	/* count references of virtual nodes */
	if(devNo == VFS_DEV_NO) {
		n->increaseRefs();
		e->node = const_cast<VFSNode*>(n);
	}
	else
		e->node = NULL;
	e->owner = pid;
	e->flags = flags;
	e->refCount = 1;
	e->usageCount = 0;
	e->position = 0;
	e->devNo = devNo;
	e->nodeNo = nodeNo;
	e->path = NULL;
	return 0;
}

void OpenFile::releaseFile(OpenFile *file) {
	SpinLock::acquire(&gftLock);
	file->next = gftFreeList;
	gftFreeList = file;
	SpinLock::release(&gftLock);
}
