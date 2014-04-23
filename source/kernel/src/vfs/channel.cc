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

#include <sys/common.h>
#include <sys/mem/cache.h>
#include <sys/mem/virtmem.h>
#include <sys/task/thread.h>
#include <sys/task/proc.h>
#include <sys/task/filedesc.h>
#include <sys/vfs/vfs.h>
#include <sys/vfs/node.h>
#include <sys/vfs/channel.h>
#include <sys/vfs/device.h>
#include <sys/vfs/openfile.h>
#include <sys/video.h>
#include <sys/spinlock.h>
#include <sys/log.h>
#include <esc/messages.h>
#include <ipc/ipcbuf.h>
#include <ipc/proto/file.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#define PRINT_MSGS			0

extern SpinLock waitLock;

uint16_t VFSChannel::nextRid = 1;

VFSChannel::VFSChannel(pid_t pid,VFSNode *p,bool &success)
		/* permissions are basically irrelevant here since the userland can't open a channel directly. */
		/* but in order to allow devices to be created by non-root users, give permissions for everyone */
		/* otherwise, if root uses that device, the driver is unable to open this channel. */
		: VFSNode(pid,generateId(pid),MODE_TYPE_CHANNEL | 0777,success), fd(-1),
		  closed(false), shmem(NULL), shmemSize(0), sendList(), recvList() {
	if(!success)
		return;

	/* auto-destroy on the last close() */
	refCount--;
	append(p);
}

void VFSChannel::invalidate() {
	/* notify potentially waiting clients */
	Sched::wakeup(EV_RECEIVED_MSG,(evobj_t)this);
	// luckily, we have the treelock here which is also used for all other calls to addMsgs/remMsgs.
	// but we get the number of messages in VFS::waitFor() without the treelock. but in this case it
	// doesn't really hurt to remove messages, because we would just give up waiting once, probably
	// call getwork afterwards which will see that there is actually no new message anymore.
	// (we can't acquire the waitlock because we use these locks in the opposite order below)
	// note also that we only get here if there are no references to this node anymore. so it's safe
	// to access the lists.
	if(getParent()) {
		static_cast<VFSDevice*>(getParent())->remMsgs(sendList.length());
		static_cast<VFSDevice*>(getParent())->clientRemoved(this);
	}
	recvList.deleteAll();
	sendList.deleteAll();
}

int VFSChannel::isSupported(int op) const {
	if(!isAlive())
		return -EDESTROYED;
	/* if the driver doesn't implement read, its an error */
	if(!static_cast<VFSDevice*>(parent)->supports(op))
		return -ENOTSUP;
	return 0;
}

void VFSChannel::discardMsgs() {
	LockGuard<SpinLock> g(&waitLock);
	// remove from parent
	static_cast<VFSDevice*>(getParent())->remMsgs(sendList.length());

	// now clear lists
	sendList.deleteAll();
	recvList.deleteAll();
}

int VFSChannel::openForDriver() {
	OpenFile *clifile;
	VFSNode *par = getParent();
	pid_t ppid = par->getOwner();
	int res = VFS::openFile(ppid,VFS_MSGS | VFS_DEVICE,this,getNo(),VFS_DEV_NO,&clifile);
	if(res < 0)
		return res;
	/* getByPid() is ok because if the parent-node exists, the owner does always exist, too */
	Proc *pp = Proc::getByPid(ppid);
	fd = FileDesc::assoc(pp,clifile);
	if(fd < 0) {
		clifile->close(ppid);
		return fd;
	}
	return 0;
}

void VFSChannel::closeForDriver() {
	/* the parent process might be dead now (in this case he would have already closed the file) */
	VFSNode *par = getParent();
	pid_t ppid = par->getOwner();
	Proc *pp = Proc::getRef(ppid);
	if(pp) {
		/* the file might have been closed already */
		OpenFile *clifile = FileDesc::unassoc(pp,fd);
		if(clifile)
			clifile->close(ppid);
		Proc::relRef(pp);
	}
}

ssize_t VFSChannel::open(pid_t pid,const char *path,uint flags,int msgid) {
	ulong buffer[IPC_DEF_SIZE / sizeof(ulong)];
	ipc::IPCBuf ib(buffer,sizeof(buffer));
	ssize_t res = -ENOENT;
	Proc *p;
	Thread *t = Thread::getRunning();
	msgid_t mid;

	/* give the driver a file-descriptor for this new client; note that we have to do that
	 * immediatly because in close() we assume that the device has already one reference to it */
	res = openForDriver();
	if(res < 0)
		return res;

	/* do we need to send an open to the driver? */
	res = isSupported(DEV_OPEN);
	if(res == -ENOTSUP)
		return 0;
	if(res < 0)
		goto error;

	p = Proc::getByPid(pid);
	assert(p != NULL);

	/* send msg to driver */
	ib << ipc::FileOpen::Request(flags,p->getEUid(),p->getEGid(),p->getPid(),ipc::CString(path));
	if(ib.error()) {
		res = -EINVAL;
		goto error;
	}
	res = send(pid,0,msgid,ib.buffer(),ib.pos(),NULL,0);
	if(res < 0)
		goto error;

	/* receive response */
	ib.reset();
	t->addResource();
	mid = res;
	res = receive(pid,0,&mid,ib.buffer(),ib.max());
	t->remResource();
	if(res < 0)
		goto error;

	{
		ipc::FileOpen::Response r;
		ib >> r;
		if(r.res < 0) {
			res = r.res;
			goto error;
		}
		return r.res;
	}

error:
	closeForDriver();
	return res;
}

void VFSChannel::close(pid_t pid,OpenFile *file,int msgid) {
	if(!isAlive())
		unref();
	else {
		if(file->isDevice())
			destroy();
		/* if there is only the device left, do the real close */
		else if(unref() == 1) {
			send(pid,0,msgid,NULL,0,NULL,0);
			closed = true;
		}
	}
}

off_t VFSChannel::seek(A_UNUSED pid_t pid,off_t position,off_t offset,uint whence) const {
	switch(whence) {
		case SEEK_SET:
			return offset;
		case SEEK_CUR:
			return position + offset;
		default:
		case SEEK_END:
			/* not supported for devices */
			return -ESPIPE;
	}
}

size_t VFSChannel::getSize(A_UNUSED pid_t pid) const {
	return sendList.length() + recvList.length();
}

int VFSChannel::stat(pid_t pid,USER sFileInfo *info) {
	Thread *t = Thread::getRunning();
	ulong buffer[IPC_DEF_SIZE / sizeof(ulong)];
	ipc::IPCBuf ib(buffer,sizeof(buffer));

	/* send msg to fs */
	ssize_t res = send(pid,0,MSG_FS_ISTAT,NULL,0,NULL,0);
	if(res < 0)
		return res;

	/* receive response */
	msgid_t mid = res;
	t->addResource();
	res = receive(pid,0,&mid,ib.buffer(),ib.max());
	t->remResource();
	if(res < 0)
		return res;

	int err;
	ib >> err >> *info;
	if(err < 0)
		return err;
	if(ib.error())
		return -EINVAL;
	return 0;
}

static bool useSharedMem(const void *shmem,size_t shmsize,const void *buffer,size_t bufsize) {
	return shmem && (uintptr_t)buffer >= (uintptr_t)shmem &&
		(uintptr_t)buffer + bufsize > (uintptr_t)buffer &&
		(uintptr_t)buffer + bufsize <= (uintptr_t)shmem + shmsize;
}

ssize_t VFSChannel::read(pid_t pid,OpenFile *file,USER void *buffer,off_t offset,size_t count) {
	ulong ibuffer[IPC_DEF_SIZE / sizeof(ulong)];
	ipc::IPCBuf ib(ibuffer,sizeof(ibuffer));
	ssize_t res;

	if((res = isSupported(DEV_READ)) < 0)
		return res;

	/* send msg to driver */
	Thread *t = Thread::getRunning();
	bool useshm = useSharedMem(shmem,shmemSize,buffer,count);
	ib << ipc::FileRead::Request(offset,count,useshm ? ((uintptr_t)buffer - (uintptr_t)shmem) : -1);
	res = file->sendMsg(pid,MSG_FILE_READ,ib.buffer(),ib.pos(),NULL,0);
	if(res < 0)
		return res;

	/* only allow signals during that operation, if we can cancel it */
	msgid_t mid = res;
	uint flags = (isSupported(DEV_CANCEL) == 0) ? VFS_SIGNALS : 0;
	while(1) {
		/* read response and ensure that we don't get killed until we've received both messages
		 * (otherwise the channel might get in an inconsistent state) */
		t->addResource();
		ib.reset();
		res = file->receiveMsg(pid,&mid,ib.buffer(),ib.max(),flags);
		t->remResource();
		if(res < 0) {
			if(res == -EINTR) {
				int cancelRes = cancel(pid,file,mid);
				if(cancelRes == 1) {
					/* if the result is already there, get it, but don't allow signals anymore */
					flags = 0;
					continue;
				}
			}
			return res;
		}

		/* handle response */
		ipc::FileRead::Response r;
		ib >> r;
		if(r.res < 0)
			return r.res;

		if(!useshm && r.res > 0) {
			/* read data */
			t->addResource();
			r.res = file->receiveMsg(pid,&mid,buffer,count,0);
			t->remResource();
		}
		return r.res;
	}
	A_UNREACHED;
}

ssize_t VFSChannel::write(pid_t pid,OpenFile *file,USER const void *buffer,off_t offset,size_t count) {
	ulong ibuffer[IPC_DEF_SIZE / sizeof(ulong)];
	ipc::IPCBuf ib(ibuffer,sizeof(ibuffer));
	ssize_t res;
	Thread *t = Thread::getRunning();
	bool useshm = useSharedMem(shmem,shmemSize,buffer,count);

	if((res = isSupported(DEV_WRITE)) < 0)
		return res;

	/* send msg and data to driver */
	ib << ipc::FileWrite::Request(offset,count,useshm ? ((uintptr_t)buffer - (uintptr_t)shmem) : -1);
	res = file->sendMsg(pid,MSG_FILE_WRITE,ib.buffer(),ib.pos(),useshm ? NULL : buffer,count);
	if(res < 0)
		return res;

	/* only allow signals during that operation, if we can cancel it */
	msgid_t mid = res;
	uint flags = (isSupported(DEV_CANCEL) == 0) ? VFS_SIGNALS : 0;
	while(1) {
		/* read response */
		ib.reset();
		t->addResource();
		res = file->receiveMsg(pid,&mid,ib.buffer(),ib.max(),flags);
		t->remResource();
		if(res < 0) {
			if(res == -EINTR) {
				int cancelRes = cancel(pid,file,mid);
				if(cancelRes == 1) {
					/* if the result is already there, get it, but don't allow signals anymore */
					flags = 0;
					continue;
				}
			}
			return res;
		}

		ipc::FileWrite::Response r;
		ib >> r;
		return r.res;
	}
	A_UNREACHED;
}

int VFSChannel::cancel(pid_t pid,OpenFile *file,msgid_t mid) {
	Thread *t = Thread::getRunning();
	ulong ibuffer[IPC_DEF_SIZE / sizeof(ulong)];
	ipc::IPCBuf ib(ibuffer,sizeof(ibuffer));

	int res;
	if((res = isSupported(DEV_CANCEL)) < 0)
		return res;

	ib << mid;
	res = file->sendMsg(pid,MSG_DEV_CANCEL,ib.buffer(),ib.pos(),NULL,0);
	if(res < 0)
		return res;

	ib.reset();
	t->addResource();
	mid = res;
	res = file->receiveMsg(pid,&mid,ib.buffer(),ib.max(),0);
	t->remResource();
	if(res < 0)
		return res;

	/* handle response */
	ib >> res;
	return res;
}

int VFSChannel::sharefile(pid_t pid,OpenFile *file,const char *path,void *cliaddr,size_t size) {
	ulong ibuffer[IPC_DEF_SIZE / sizeof(ulong)];
	ipc::IPCBuf ib(ibuffer,sizeof(ibuffer));
	ssize_t res;
	Thread *t = Thread::getRunning();

	if(shmem != NULL)
		return -EEXIST;
	if((res = isSupported(DEV_SHFILE)) < 0)
		return res;

	/* send msg to driver */
	ib << ipc::FileShFile::Request(size,ipc::CString(path));
	if(ib.error())
		return -EINVAL;
	res = file->sendMsg(pid,MSG_DEV_SHFILE,ib.buffer(),ib.pos(),NULL,0);
	if(res < 0)
		return res;

	/* read response */
	ib.reset();
	t->addResource();
	msgid_t mid = res;
	res = file->receiveMsg(pid,&mid,ib.buffer(),ib.max(),0);
	t->remResource();
	if(res < 0)
		return res;

	/* handle response */
	ipc::FileShFile::Response r;
	ib >> r;
	if(r.res < 0)
		return r.res;
	shmem = cliaddr;
	shmemSize = size;
	return 0;
}

int VFSChannel::creatsibl(pid_t pid,OpenFile *file,VFSChannel *sibl,int arg) {
	ulong ibuffer[IPC_DEF_SIZE / sizeof(ulong)];
	ipc::IPCBuf ib(ibuffer,sizeof(ibuffer));
	Thread *t = Thread::getRunning();
	msgid_t mid;
	uint flags;

	int res;
	if((res = isSupported(DEV_CREATSIBL)) < 0)
		return res;

	/* first, give the driver a file-descriptor for the new channel */
	res = sibl->openForDriver();
	if(res < 0)
		return res;

	/* send msg to driver */
	ib << ipc::FileCreatSibl::Request(sibl->fd,arg);
	res = file->sendMsg(pid,MSG_DEV_CREATSIBL,ib.buffer(),ib.pos(),NULL,0);
	if(res < 0)
		goto error;

	/* only allow signals during that operation, if we can cancel it */
	mid = res;
	flags = (isSupported(DEV_CANCEL) == 0) ? VFS_SIGNALS : 0;
	while(1) {
		/* read response */
		ib.reset();
		t->addResource();
		res = file->receiveMsg(pid,&mid,ib.buffer(),ib.max(),flags);
		t->remResource();
		if(res < 0) {
			if(res == -EINTR) {
				int cancelRes = cancel(pid,file,mid);
				if(cancelRes == 1) {
					/* if the result is already there, get it, but don't allow signals anymore */
					flags = 0;
					continue;
				}
			}
			goto error;
		}

		ipc::FileCreatSibl::Response r;
		ib >> r;
		if(r.res < 0) {
			res = r.res;
			goto error;
		}
		return 0;
	}
	A_UNREACHED;

error:
	sibl->closeForDriver();
	return res;
}

ssize_t VFSChannel::send(A_UNUSED pid_t pid,ushort flags,msgid_t id,USER const void *data1,
                         size_t size1,USER const void *data2,size_t size2) {
	SList<Message> *list;
	Message *msg1,*msg2 = NULL;

	/* devices write to the receive-list (which will be read by other processes) */
	if(flags & VFS_DEVICE) {
		assert(data2 == NULL && size2 == 0);
		list = &recvList;
	}
	/* other processes write to the send-list (which will be read by the driver) */
	else
		list = &sendList;

	/* create message and copy data to it */
	msg1 = (Message*)Cache::alloc(sizeof(Message) + size1);
	if(EXPECT_FALSE(msg1 == NULL))
		return -ENOMEM;

	msg1->length = size1;
	if(EXPECT_TRUE(data1)) {
		Thread::addHeapAlloc(msg1);
		memcpy(msg1 + 1,data1,size1);
		Thread::remHeapAlloc(msg1);
	}

	if(EXPECT_FALSE(data2)) {
		msg2 = (Message*)Cache::alloc(sizeof(Message) + size2);
		if(EXPECT_FALSE(msg2 == NULL))
			return -ENOMEM;

		msg2->length = size2;
		Thread::addHeapAlloc(msg1);
		Thread::addHeapAlloc(msg2);
		memcpy(msg2 + 1,data2,size2);
		Thread::remHeapAlloc(msg2);
		Thread::remHeapAlloc(msg1);
	}

	{
		/* note that we do that here, because memcpy can fail because the page is swapped out for
		 * example. we can't hold the lock during that operation */
		LockGuard<SpinLock> g(&waitLock);

		/* set request id. for clients, we generate a new unique request-id */
		if(list == &sendList) {
			id &= 0xFFFF;
			/* prevent to set the MSB. otherwise the return-value would be negative (on 32-bit) */
			id |= ((nextRid++) & 0x7FFF) << 16;
		}
		/* for devices, we just use whatever the driver gave us */
		msg1->id = id;
		if(EXPECT_FALSE(data2))
			msg2->id = id;

		/* append to list */
		list->append(msg1);
		if(EXPECT_FALSE(msg2))
			list->append(msg2);

		/* notify the driver */
		if(list == &sendList) {
			static_cast<VFSDevice*>(parent)->addMsgs(1);
			if(EXPECT_FALSE(msg2))
				static_cast<VFSDevice*>(parent)->addMsgs(1);
			Sched::wakeup(EV_CLIENT,(evobj_t)parent,true);
		}
		else {
			/* notify other possible waiters */
			Sched::wakeup(EV_RECEIVED_MSG,(evobj_t)this,true);
		}
	}

#if PRINT_MSGS
	Thread *t = Thread::getRunning();
	Proc *p = Proc::getByPid(pid);
	Log::get().writef("%2d:%2d(%-12.12s) -> %5u:%5u (%4d b) %#x (%s)\n",
			t->getTid(),pid,p ? p->getProgram() : "??",id >> 16,id & 0xFFFF,size1,this,getPath());
	if(data2) {
		Log::get().writef("%2d:%2d(%-12.12s) -> %5u:%5u (%4d b) %#x (%s)\n",
				t->getTid(),pid,p ? p->getProgram() : "??",id >> 16,id & 0xFFFF,size2,this,getPath());
	}
#endif
	return id;
}

ssize_t VFSChannel::receive(A_UNUSED pid_t pid,ushort flags,msgid_t *id,USER void *data,size_t size) {
	SList<Message> *list;
	Thread *t = Thread::getRunning();
	VFSNode *waitNode;
	Message *msg;
	size_t event;
	ssize_t res;

	/* determine list and event to use */
	if(flags & VFS_DEVICE) {
		event = EV_CLIENT;
		list = &sendList;
		waitNode = parent;
	}
	else {
		event = EV_RECEIVED_MSG;
		list = &recvList;
		waitNode = this;
	}

	/* wait until a message arrives */
	waitLock.down();
	while((msg = getMsg(list,*id,flags)) == NULL) {
		if(EXPECT_FALSE(flags & VFS_NOBLOCK)) {
			waitLock.up();
			return -EWOULDBLOCK;
		}
		/* if the channel has already been closed, there is no hope of success here */
		if(EXPECT_FALSE(closed || !isAlive())) {
			waitLock.up();
			return -EDESTROYED;
		}
		t->wait(event,(evobj_t)waitNode);
		waitLock.up();

		if(flags & VFS_SIGNALS) {
			Thread::switchAway();
			if(EXPECT_FALSE(t->hasSignal()))
				return -EINTR;
		}
		else
			Thread::switchNoSigs();

		/* if we waked up and there is no message, the driver probably died */
		if(EXPECT_FALSE(!isAlive()))
			return -EDESTROYED;
		waitLock.down();
	}

	if(event == EV_CLIENT)
		static_cast<VFSDevice*>(parent)->remMsgs(1);
	waitLock.up();

#if PRINT_MSGS
	Proc *p = Proc::getByPid(pid);
	Log::get().writef("%2d:%2d(%-12.12s) <- %5u:%5u (%4d b) %#x (%s)\n",
			t->getTid(),pid,p ? p->getProgram() : "??",msg->id >> 16,msg->id & 0xFFFF,
			msg->length,this,getPath());
#endif

	if(EXPECT_FALSE(data && msg->length > size)) {
		Log::get().writef("INVALID: len=%zu, size=%zu\n",msg->length,size);
		Cache::free(msg);
		return -EINVAL;
	}

	/* copy data and id; since it may fail we have to ensure that our resources are free'd */
	Thread::addHeapAlloc(msg);
	if(EXPECT_TRUE(data))
		memcpy(data,msg + 1,msg->length);
	if(EXPECT_FALSE(id))
		*id = msg->id;
	Thread::remHeapAlloc(msg);

	res = msg->length;
	Cache::free(msg);
	return res;
}

VFSChannel::Message *VFSChannel::getMsg(SList<Message> *list,msgid_t mid,ushort flags) {
	/* drivers get always the first message */
	if(flags & VFS_DEVICE)
		return list->removeFirst();

	/* find the message for the given id */
	Message *p = NULL;
	for(auto it = list->begin(); it != list->end(); p = &*it, ++it) {
		/* either it's a "for anybody" message, or the id has to match */
		if(it->id == mid || (it->id >> 16) == 0) {
			list->removeAt(p,&*it);
			return &*it;
		}
	}
	return NULL;
}

void VFSChannel::print(OStream &os) const {
	const SList<Message> *lists[] = {&sendList,&recvList};
	os.writef("%-8s: snd=%zu rcv=%zu closed=%d fd=%02d shm=%zuK\n",
		name,sendList.length(),recvList.length(),closed,fd,shmem ? shmemSize / 1024 : 0);
	for(size_t i = 0; i < ARRAY_SIZE(lists); i++) {
		for(auto it = lists[i]->cbegin(); it != lists[i]->cend(); ++it) {
			os.writef("\t%s id=%u:%u len=%zu\n",i == 0 ? "->" : "<-",
				it->id >> 16,it->id & 0xFFFF,it->length);
		}
	}
}
