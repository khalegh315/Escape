/**
 * @version		$Id$
 * @author		Nils Asmussen <nils@script-solution.de>
 * @copyright	2008 Nils Asmussen
 */

#include "../h/common.h"
#include "../h/proc.h"
#include "../h/paging.h"
#include "../h/mm.h"
#include "../h/util.h"
#include "../h/string.h"
#include "../h/video.h"
#include "../h/intrpt.h"
#include "../h/sched.h"
#include "../h/vfs.h"
#include "../h/kheap.h"

/* public process-data */
typedef struct {
	u8 state;
	tPid pid;
	tPid parentPid;
	u32 textPages;
	u32 dataPages;
	u32 stackPages;
} sProcPub;

/* our processes */
static sProc procs[PROC_COUNT];
/* TODO keep that? */
/* the process-index */
static tPid pi;

/* pointer to a dead proc that has to be deleted */
static sProc *deadProc = NULL;

/**
 * Our VFS read handler that should read process information into a given buffer
 *
 * @param node the VFS node
 * @param buffer the buffer where to copy the info to
 * @param offset the offset where to start
 * @param count the number of bytes
 * @return the number of read bytes
 */
static s32 proc_vfsReadHandler(sVFSNode *node,u8 *buffer,u32 offset,u32 count) {
	/* don't use the cache here to prevent that one process occupies it for all others */
	/* (if the process doesn't call close() the cache will not be invalidated and therefore
	 * other processes might miss changes) */
	sProc *p = procs + atoi(node->name);
	sProcPub *proc;
	/* can we copy it directly? */
	if(offset == 0 && count == sizeof(sProcPub))
		proc = (sProcPub*)buffer;
	/* don't waste time in this case */
	else if(offset >= sizeof(sProcPub))
		return 0;
	/* ok, use the heap as temporary storage */
	else {
		proc = kheap_alloc(sizeof(sProcPub));
		if(proc == NULL)
			return 0;
	}

	/* copy values to public struct */
	proc->state = p->state;
	proc->pid = p->pid;
	proc->parentPid = p->parentPid;
	proc->textPages = p->textPages;
	proc->dataPages = p->dataPages;
	proc->stackPages = p->stackPages;

	/* stored on kheap? */
	if((u32)proc != (u32)buffer) {
		/* correct vars */
		if(offset > sizeof(sProcPub))
			offset = sizeof(sProcPub);
		count = MIN(sizeof(sProcPub) - offset,count);
		/* copy */
		if(count > 0)
			memcpy(buffer,(u8*)proc + offset,count);
		/* free temp storage */
		kheap_free(proc);
	}

	return count;
}

/**
 * Creates a VFS node for the given pid
 *
 * @param pid the process-id
 * @return true if successfull
 */
static bool proc_createVFSNode(tPid pid) {
	string name = (string)kheap_alloc(sizeof(s8) * 12);
	if(name == NULL)
		return false;

	itoa(name,pid);
	return vfs_createProcessNode(name,&proc_vfsReadHandler);
}

void proc_init(void) {
	tFD i;
	/* init the first process */
	pi = 0;
	if(!proc_createVFSNode(0))
		panic("Not enough mem for init process");
	procs[pi].state = ST_RUNNING;
	procs[pi].pid = 0;
	procs[pi].parentPid = 0;
	/* the first process has no text, data and stack */
	procs[pi].textPages = 0;
	procs[pi].dataPages = 0;
	procs[pi].stackPages = 0;
	/* note that this assumes that the page-dir is initialized */
	procs[pi].physPDirAddr = (u32)paging_getProc0PD() & ~KERNEL_AREA_V_ADDR;
	/* init fds */
	for(i = 0; i < MAX_FD_COUNT; i++)
		procs[pi].fileDescs[i] = -1;

	/* setup kernel-stack for us */
	paging_map(KERNEL_STACK,NULL,1,PG_WRITABLE | PG_SUPERVISOR,false);
}

tPid proc_getFreePid(void) {
	tPid pid;
	/* we can skip our initial process */
	for(pid = 1; pid < PROC_COUNT; pid++) {
		if(procs[pid].state == ST_UNUSED)
			return pid;
	}
	return 0;
}

sProc *proc_getRunning(void) {
	return &procs[pi];
}

sProc *proc_getByPid(tPid pid) {
	return &procs[pid];
}

void proc_switch(void) {
	sProc *p = procs + pi;
	vid_printf("Free memory: %d KiB\n",mm_getNumberOfFreeFrames(MM_DEF) * PAGE_SIZE / K);
	vid_printf("Process %d\n",p->pid);
	if(!proc_save(&p->save)) {
		/* select next process */
		p = sched_perform();
		pi = p->pid;
		vid_printf("Resuming %d\n",p->pid);
		proc_resume(p->physPDirAddr,&p->save);
	}

	/* destroy process, if there is any */
	if(deadProc != NULL) {
		proc_destroy(deadProc);
		deadProc = NULL;
	}

	vid_printf("Continuing %d\n",p->pid);
}

tFile proc_fdToFile(tFD fd) {
	tFile fileNo;
	if(fd >= MAX_FD_COUNT)
		return ERR_INVALID_FD;

	fileNo = (procs + pi)->fileDescs[fd];
	if(fileNo == -1)
		return ERR_INVALID_FD;

	return fileNo;
}

s32 proc_openFile(tFile fileNo) {
	tFD i;
	tFile *fds = procs[pi].fileDescs;
	for(i = 0; i < MAX_FD_COUNT; i++) {
		if(fds[i] == -1) {
			fds[i] = fileNo;
			return i;
		}
	}

	return ERR_MAX_PROC_FDS;
}

tFile proc_closeFile(tFD fd) {
	tFile fileNo;
	if(fd >= MAX_FD_COUNT)
		return ERR_INVALID_FD;

	fileNo = procs[pi].fileDescs[fd];
	if(fileNo == -1)
		return ERR_INVALID_FD;

	procs[pi].fileDescs[fd] = -1;
	return fileNo;
}

s32 proc_clone(tPid newPid) {
	u32 i,pdirFrame,stackFrame;
	u32 *src,*dst;
	sProc *p;

	if((procs + newPid)->state != ST_UNUSED)
		panic("The process slot 0x%x is already in use!",procs + newPid);

	/* first create the VFS node (we may not have enough mem) */
	if(!proc_createVFSNode(newPid))
		return -1;

	/* clone page-dir */
	if((pdirFrame = paging_clonePageDir(&stackFrame,procs + newPid)) == 0)
		return -1;

	/* set page-dir and pages for segments */
	p = procs + newPid;
	p->pid = newPid;
	p->parentPid = pi;
	p->textPages = procs[pi].textPages;
	p->dataPages = procs[pi].dataPages;
	p->stackPages = procs[pi].stackPages;
	p->physPDirAddr = pdirFrame << PAGE_SIZE_SHIFT;
	/* init fds */
	for(i = 0; i < MAX_FD_COUNT; i++)
		p->fileDescs[i] = -1;
	/* make ready */
	p->state = ST_READY;
	sched_enqueueReady(p);

	/* map stack temporary (copy later) */
	paging_map(KERNEL_STACK_TMP,&stackFrame,1,PG_SUPERVISOR | PG_WRITABLE,true);

	/* save us first */
	if(proc_save(&p->save)) {
		/* child */
		return 1;
	}

	/* now copy the stack */
	/* copy manually to prevent function-call (otherwise we would change the stack) */
	src = (u32*)KERNEL_STACK;
	dst = (u32*)KERNEL_STACK_TMP;
	for(i = 0; i < PT_ENTRY_COUNT; i++)
		*dst++ = *src++;
	/* unmap it */
	paging_unmap(KERNEL_STACK_TMP,1,false);

	/* parent */
	return 0;
}

void proc_suicide(void) {
	if(pi == 0)
		panic("The initial process has to be alive!!");

	/* mark ourself as destroyable */
	deadProc = procs + pi;
	/* ensure that we will not be selected on the next resched */
	deadProc->state = ST_ZOMBIE;
}

void proc_destroy(sProc *p) {
	tFD i;
	/* don't delete initial or unused processes */
	if(p->pid == 0 || p->state == ST_UNUSED)
		panic("The process @ 0x%x with pid=%d is unused or the initial process",p,p->pid);
	if(p->pid == pi)
		panic("You can't destroy yourself!");

	/* destroy paging-structure */
	paging_destroyPageDir(p);

	/* release file-descriptors */
	for(i = 0; i < MAX_FD_COUNT; i++) {
		if(p->fileDescs[i] != -1) {
			vfs_closeFile(p->fileDescs[i]);
			p->fileDescs[i] = -1;
		}
	}

	/* remove from VFS */
	vfs_removeProcessNode(p->pid);

	/* mark as unused */
	p->textPages = 0;
	p->dataPages = 0;
	p->stackPages = 0;
	/* remove from ready-queue, if necessary */
	if(p->state == ST_READY)
		sched_dequeueProc(p);
	p->state = ST_UNUSED;
	p->pid = 0;
	p->physPDirAddr = 0;
}

void proc_setupIntrptStack(sIntrptStackFrame *frame) {
	frame->uesp = KERNEL_AREA_V_ADDR - sizeof(u32);
	frame->ebp = frame->uesp;
	/* user-mode segments */
	/* TODO remove the hard-coded stuff here! */
	frame->cs = 0x1b;
	frame->ds = 0x23;
	frame->es = 0x23;
	frame->fs = 0x23;
	frame->gs = 0x23;
	frame->uss = 0x23;
	/* TODO entry-point */
	frame->eip = 0;
	/* general purpose register */
	frame->eax = 0;
	frame->ebx = 0;
	frame->ecx = 0;
	frame->edx = 0;
	frame->esi = 0;
	frame->edi = 0;
}

bool proc_segSizesValid(u32 textPages,u32 dataPages,u32 stackPages) {
	u32 maxPages = KERNEL_AREA_V_ADDR / PAGE_SIZE;
	return textPages + dataPages + stackPages <= maxPages;
}

bool proc_changeSize(s32 change,eChgArea area) {
	u32 addr,chg = change,origPages;
	/* determine start-address */
	if(area == CHG_DATA) {
		addr = (procs[pi].textPages + procs[pi].dataPages) * PAGE_SIZE;
		origPages = procs[pi].textPages + procs[pi].dataPages;
	}
	else {
		addr = KERNEL_AREA_V_ADDR - (procs[pi].stackPages + change) * PAGE_SIZE;
		origPages = procs[pi].stackPages;
	}

	if(change > 0) {
		u32 ts,ds,ss;
		/* not enough mem? */
		if(mm_getNumberOfFreeFrames(MM_DEF) < paging_countFramesForMap(addr,change)) {
			return false;
		}
		/* invalid segment sizes? */
		ts = procs[pi].textPages;
		ds = procs[pi].dataPages;
		ss = procs[pi].stackPages;
		if((area == CHG_DATA && !proc_segSizesValid(ts,ds + change,ss))
				|| (area == CHG_STACK && !proc_segSizesValid(ts,ds,ss + change))) {
			return false;
		}

		paging_map(addr,NULL,change,PG_WRITABLE,false);

		/* now clear the memory */
		while(change-- > 0) {
			memset((void*)addr,0,PAGE_SIZE);
			addr += PAGE_SIZE;
		}
	}
	else {
		/* we have to correct the address */
		addr += change * PAGE_SIZE;

		/* free and unmap memory */
		paging_unmap(addr,-change,true);

		/* can we remove all page-tables? */
		if(origPages + change == 0) {
			paging_unmapPageTables(ADDR_TO_PDINDEX(addr),PAGES_TO_PTS(-change));
		}
		/* ok, remove just the free ones */
		else {
			/* at first calculate the max. pts we may free (based on the pages we removed) */
			s32 start = ADDR_TO_PDINDEX(addr & ~(PAGE_SIZE * PT_ENTRY_COUNT - 1));
			s32 count = ADDR_TO_PDINDEX(addr - (change + 1) * PAGE_SIZE) - ADDR_TO_PDINDEX(addr) + 1;
			/* don't delete the first pt? */
			if(area == CHG_DATA && (addr & (PAGE_SIZE * PT_ENTRY_COUNT - 1)) > 0) {
				start++;
				count--;
			}
			/* don't delete last pt? */
			else if(area == CHG_STACK &&
					((addr - change * PAGE_SIZE) & (PAGE_SIZE * PT_ENTRY_COUNT - 1)) > 0) {
				count--;
			}

			/* finally unmap the pts */
			if(count > 0)
				paging_unmapPageTables(start,count);
		}
	}

	/* adjust sizes */
	if(area == CHG_DATA) {
		procs[pi].dataPages += chg;
	}
	else {
		procs[pi].stackPages += chg;
	}

	return true;
}
