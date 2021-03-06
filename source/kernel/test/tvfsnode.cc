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

#include <sys/test.h>
#include <task/filedesc.h>
#include <task/proc.h>
#include <task/thread.h>
#include <vfs/node.h>
#include <vfs/openfile.h>
#include <vfs/vfs.h>
#include <common.h>
#include <errno.h>
#include <string.h>
#include <video.h>

#include "testutils.h"

static void test_vfsn();
static void test_vfs_node_resolvePath();
static bool test_vfs_node_resolvePathCpy(const char *a,const char *b);
static void test_vfs_node_getPath();
static void test_vfs_node_file();
static void test_vfs_node_file_refs();
static void test_vfs_node_dir_refs();
static void test_vfs_node_dev_refs();

/* our test-module */
sTestModule tModVFSn = {
	"VFSNode",
	&test_vfsn
};

static void test_vfsn() {
	test_vfs_node_resolvePath();
	test_vfs_node_getPath();
	test_vfs_node_file();
	test_vfs_node_file_refs();
	test_vfs_node_dir_refs();
	test_vfs_node_dev_refs();
}

static void test_vfs_node_resolvePath() {
	test_caseStart("Testing vfs_node_resolvePath()");

	if(!test_vfs_node_resolvePathCpy("/sys/..","")) return;
	if(!test_vfs_node_resolvePathCpy("/sys//../..","")) return;
	if(!test_vfs_node_resolvePathCpy("/sys//./.","sys")) return;
	if(!test_vfs_node_resolvePathCpy("/sys/","sys")) return;
	if(!test_vfs_node_resolvePathCpy("/sys//","sys")) return;
	if(!test_vfs_node_resolvePathCpy("/sys///","sys")) return;
	if(!test_vfs_node_resolvePathCpy("/sys/proc/..","sys")) return;
	if(!test_vfs_node_resolvePathCpy("/sys/proc/.","proc")) return;
	if(!test_vfs_node_resolvePathCpy("/sys/proc/./","proc")) return;
	if(!test_vfs_node_resolvePathCpy("/sys/./.","sys")) return;
	if(!test_vfs_node_resolvePathCpy("/sys/////proc/./././.","proc")) return;
	if(!test_vfs_node_resolvePathCpy("/sys/./proc/../proc/./","proc")) return;
	if(!test_vfs_node_resolvePathCpy("/sys//..//..//..","")) return;

	test_caseSucceeded();
}

static bool test_vfs_node_resolvePathCpy(const char *a,const char *b) {
	int err;
	VFSNode *node = NULL;
	if((err = VFSNode::request(fs::User::kernel(),a,&node,VFS_READ,0)) != 0) {
		test_caseFailed("Unable to resolve the path %s",a);
		return false;
	}

	bool res = test_assertStr(node->getName(),(char*)b);
	VFSNode::release(node);
	return res;
}

static void test_vfs_node_getPath() {
	VFSNode *node;
	const fs::User kern = fs::User::kernel();

	test_caseStart("Testing vfs_node_getPath()");

	node = NULL;
	VFSNode::request(kern,"/sys",&node,VFS_READ,0);
	test_assertStr(node->getPath(),(char*)"/sys");
	VFSNode::release(node);

	node = NULL;
	VFSNode::request(kern,"/sys/proc",&node,VFS_READ,0);
	test_assertStr(node->getPath(),(char*)"/sys/proc");
	VFSNode::release(node);

	node = NULL;
	VFSNode::request(kern,"/sys/proc/0",&node,VFS_READ,0);
	test_assertStr(node->getPath(),(char*)"/sys/proc/0");
	VFSNode::release(node);

	test_caseSucceeded();
}

static void test_vfs_node_file() {
	char buf1[64] = "This is a test!";
	char buf2[64];
	Thread *t = Thread::getRunning();
	pid_t pid = t->getProc()->getPid();

	test_caseStart("Testing VFS files");

	checkMemoryBefore(false);
	size_t nodesBefore = VFSNode::getNodeCount();
	{
		OpenFile *f1,*f2;
		test_assertInt(VFS::openPath(pid,VFS_READ | VFS_WRITE | VFS_CREATE,0,"/tmp/foobar",NULL,&f1),0);

		test_assertSSize(f1->read(buf1,sizeof(buf1)),0);
		test_assertSSize(f1->write(buf1,strlen(buf1) + 1),strlen(buf1) + 1);
		f1->seek(0,SEEK_SET);
		test_assertSSize(f1->read(buf2,sizeof(buf2)),strlen(buf1) + 1);
		test_assertStr(buf2,buf1);

		test_assertInt(f1->truncate(8 * 1024 * 1024),0);
		f1->seek(0,SEEK_SET);
		test_assertSSize(f1->write(buf1,strlen(buf1) + 1),strlen(buf1) + 1);
		f1->seek(8 * 1024,SEEK_SET);
		test_assertSSize(f1->write(buf1,strlen(buf1) + 1),strlen(buf1) + 1);
		f1->seek(24 * 1024,SEEK_SET);
		test_assertSSize(f1->write(buf1,strlen(buf1) + 1),strlen(buf1) + 1);
		f1->seek(512 * 1024,SEEK_SET);
		test_assertSSize(f1->write(buf1,strlen(buf1) + 1),strlen(buf1) + 1);
		f1->seek(7 * 1024 * 1024,SEEK_SET);
		test_assertSSize(f1->write(buf1,strlen(buf1) + 1),strlen(buf1) + 1);

		test_assertInt(VFS::openPath(pid,VFS_WRITE,0,"/tmp",NULL,&f2),0);
		test_assertInt(f2->unlink("foobar"),0);

		f1->close();
	}
	test_assertSize(nodesBefore,VFSNode::getNodeCount());
	checkMemoryAfter(false);

	test_caseSucceeded();
}

static void test_vfs_node_file_refs() {
	Thread *t = Thread::getRunning();
	pid_t pid = t->getProc()->getPid();
	OpenFile *f1,*f2,*f3;
	VFSNode *n;
	char buffer[64] = "This is a test!";

	test_caseStart("Testing reference counting of file nodes");
	checkMemoryBefore(false);
	size_t nodesBefore = VFSNode::getNodeCount();

	test_assertInt(VFS::openPath(pid,VFS_WRITE | VFS_CREATE,0,"/sys/foobar",NULL,&f1),0);
	n = f1->getNode();
	test_assertSSize(f1->write(buffer,strlen(buffer)),strlen(buffer));
	test_assertSize(n->getRefCount(),2);
	f1->close();
	test_assertSize(n->getRefCount(),1);

	test_assertInt(VFS::openPath(pid,VFS_READ,0,"/sys/foobar",NULL,&f2),0);
	test_assertSize(n->getRefCount(),2);

	test_assertInt(VFS::openPath(pid,VFS_WRITE,0,"/sys",NULL,&f1),0);
	test_assertInt(f1->unlink("foobar"),0);
	f1->close();

	test_assertSize(n->getRefCount(),1);
	test_assertInt(VFS::openPath(pid,VFS_READ,0,"/sys/foobar",NULL,&f3),-ENOENT);
	memclear(buffer,sizeof(buffer));
	test_assertTrue(f2->read(buffer,sizeof(buffer)) > 0);
	test_assertStr(buffer,"This is a test!");
	f2->close();

	test_assertSize(nodesBefore,VFSNode::getNodeCount());
	checkMemoryAfter(false);
	test_caseSucceeded();
}

static void test_vfs_node_dir_refs() {
	Thread *t = Thread::getRunning();
	pid_t pid = t->getProc()->getPid();
	OpenFile *f1,*f2;
	VFSNode *n;
	bool valid;

	test_caseStart("Testing reference counting of dir nodes");
	checkMemoryBefore(false);
	size_t nodesBefore = VFSNode::getNodeCount();

	test_assertInt(VFS::openPath(pid,VFS_WRITE,0,"/sys",NULL,&f1),0);
	test_assertInt(f1->mkdir("foobar",DIR_DEF_MODE),0);
	f1->close();

	test_assertInt(VFS::openPath(pid,VFS_WRITE,0,"/sys/foobar",NULL,&f1),0);
	test_assertInt(f1->mkdir("test",DIR_DEF_MODE),0);
	f1->close();

	test_assertInt(VFS::openPath(pid,VFS_WRITE | VFS_CREATE,0,"/sys/foobar/myfile1",NULL,&f1),0);
	f1->close();
	test_assertInt(VFS::openPath(pid,VFS_WRITE | VFS_CREATE,0,"/sys/foobar/myfile2",NULL,&f1),0);
	f1->close();

	n = NULL;
	test_assertInt(VFSNode::request(fs::User::kernel(),"/sys/foobar",&n,0,0),0);
	/* foobar itself, "test", "myfile1", "myfile2" and the request of "foobar" = 5 */
	test_assertSize(n->getRefCount(),5);
	VFSNode::release(n);
	test_assertSize(n->getRefCount(),4);
	test_assertInt(VFS::openPath(pid,VFS_READ,0,"/sys/foobar",NULL,&f1),0);
	test_assertSize(n->getRefCount(),5);

	const VFSNode *f = n->openDir(false,&valid);
	test_assertTrue(valid);

	test_assertStr(f->getName(),"myfile2");
	f = f->next;
	test_assertStr(f->getName(),"myfile1");
	f = f->next;
	test_assertStr(f->getName(),"test");

	test_assertInt(VFS::openPath(pid,VFS_WRITE,0,"/sys/foobar",NULL,&f2),0);
	test_assertSize(n->getRefCount(),6);

	test_assertInt(f2->rmdir("test"),0);
	test_assertSize(n->getRefCount(),5);

	test_assertInt(f2->unlink("myfile1"),0);
	test_assertSize(n->getRefCount(),4);

	test_assertInt(f2->unlink("myfile2"),0);
	test_assertSize(n->getRefCount(),3);
	f2->close();
	test_assertSize(n->getRefCount(),2);

	test_assertInt(VFS::openPath(pid,VFS_WRITE,0,"/sys",NULL,&f2),0);
	test_assertSize(n->getRefCount(),2);

	test_assertInt(f2->rmdir("foobar"),0);
	test_assertSize(n->getRefCount(),1);
	f2->close();

	n->closeDir(false);
	f1->close();

	test_assertSize(nodesBefore,VFSNode::getNodeCount());
	checkMemoryAfter(false);
	test_caseSucceeded();
}

static void test_vfs_node_dev_refs() {
	char path[MAX_PATH_LEN];
	Thread *t = Thread::getRunning();
	Proc *p = t->getProc();
	pid_t pid = p->getPid();
	OpenFile *f1,*f2,*f3;
	OpenFile *d1,*d2;

	test_caseStart("Testing reference counting of device/channel nodes");
	checkMemoryBefore(false);
	size_t nodesBefore = VFSNode::getNodeCount();

	strnzcpy(path,"/dev/foo",sizeof(path));
	test_assertInt(VFS::openPath(pid,VFS_MSGS | VFS_WRITE,0,"/dev",NULL,&f2),0);
	test_assertInt(f2->createdev("foo",0777,DEV_TYPE_BLOCK,DEV_CLOSE,&f1),0);
	f2->close();
	test_assertSize(f1->getNode()->getRefCount(),1);

	test_assertInt(VFS::openPath(pid,VFS_MSGS,0,"/dev/foo",NULL,&f2),0);
	d1 = FileDesc::request(p,3);
	test_assertPtr(d1->getNode(),f2->getNode());
	test_assertSize(f1->getNode()->getRefCount(),2);
	test_assertSize(f2->getNode()->getRefCount(),3);

	test_assertInt(VFS::openPath(pid,VFS_MSGS,0,"/dev/foo",NULL,&f3),0);
	d2 = FileDesc::request(p,4);
	test_assertPtr(d2->getNode(),f3->getNode());

	/* 2 channels; 2 files each */
	test_assertSize(f1->getNode()->getRefCount(),3);
	test_assertSize(f3->getNode()->getRefCount(),3);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 3);

	f3->close();
	/* 2 channels; d1=2 files, d2=1 file */
	test_assertSize(f1->getNode()->getRefCount(),3);
	test_assertSize(f2->getNode()->getRefCount(),3);
	test_assertSize(d2->getNode()->getRefCount(),2);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 3);

	d2->close();
	FileDesc::unassoc(p,4);
	/* 1 channel; d1=2 files, d2 gone */
	test_assertSize(f1->getNode()->getRefCount(),2);
	test_assertSize(f2->getNode()->getRefCount(),3);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 2);

	f2->close();
	/* 1 channel; d1=1 file */
	test_assertSize(f1->getNode()->getRefCount(),2);
	test_assertSize(d1->getNode()->getRefCount(),2);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 2);

	d1->close();
	FileDesc::unassoc(p,3);
	/* 0 channels */
	test_assertSize(f1->getNode()->getRefCount(),1);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 1);

	test_assertInt(VFS::openPath(pid,VFS_MSGS,0,"/dev/foo",NULL,&f2),0);
	d1 = FileDesc::request(p,3);
	/* 1 channel; d1=2 files */
	test_assertPtr(d1->getNode(),f2->getNode());
	test_assertSize(f1->getNode()->getRefCount(),2);
	test_assertSize(f2->getNode()->getRefCount(),3);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 2);

	d1->close();
	FileDesc::unassoc(p,3);
	/* 1 channel; d1=1 file */
	test_assertSize(f1->getNode()->getRefCount(),2);
	test_assertSize(f2->getNode()->getRefCount(),2);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 2);

	f2->close();
	/* 0 channels */
	test_assertSize(f1->getNode()->getRefCount(),1);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 1);

	test_assertInt(VFS::openPath(pid,VFS_MSGS,0,"/dev/foo",NULL,&f2),0);
	d1 = FileDesc::request(p,3);
	/* 1 channel; d1=2 files */
	test_assertPtr(d1->getNode(),f2->getNode());
	test_assertSize(f1->getNode()->getRefCount(),2);
	test_assertSize(f2->getNode()->getRefCount(),3);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 2);

	f1->close();
	/* 1 channel; d1=2 files */
	test_assertSize(f2->getNode()->getRefCount(),2);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 2);

	d1->close();
	FileDesc::unassoc(p,3);
	/* 1 channel; d1=1 file */
	test_assertSize(f2->getNode()->getRefCount(),1);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 2);

	f2->close();
	/* 0 channels */
	test_assertSize(VFSNode::getNodeCount(),nodesBefore);

	test_assertInt(VFS::openPath(pid,VFS_MSGS | VFS_WRITE,0,"/dev",NULL,&f2),0);
	test_assertInt(f2->createdev("foo",0777,DEV_TYPE_BLOCK,DEV_CLOSE,&f1),0);
	f2->close();
	test_assertSize(f1->getNode()->getRefCount(),1);

	test_assertInt(VFS::openPath(pid,VFS_MSGS,0,"/dev/foo",NULL,&f2),0);
	d1 = FileDesc::request(p,3);
	/* 1 channel; d1=2 files */
	test_assertPtr(d1->getNode(),f2->getNode());
	test_assertSize(f1->getNode()->getRefCount(),2);
	test_assertSize(f2->getNode()->getRefCount(),3);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 2);

	d1->close();
	FileDesc::unassoc(p,3);
	/* 1 channel; d1=1 file */
	test_assertSize(f2->getNode()->getRefCount(),2);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 2);

	f1->close();
	/* 1 channel; d1=1 file (no change; chan already destroyed) */
	test_assertSize(f2->getNode()->getRefCount(),2);
	test_assertSize(VFSNode::getNodeCount(),nodesBefore + 2);

	f2->close();
	/* 0 channels */
	test_assertSize(VFSNode::getNodeCount(),nodesBefore);

	checkMemoryAfter(false);
	test_caseSucceeded();
}
