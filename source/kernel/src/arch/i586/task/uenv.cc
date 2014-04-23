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
#include <sys/arch/i586/gdt.h>
#include <sys/task/uenv.h>
#include <sys/task/thread.h>
#include <sys/task/proc.h>
#include <sys/mem/virtmem.h>
#include <sys/mem/pagedir.h>
#include <sys/spinlock.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

void UEnv::startSignalHandler(Thread *t,IntrptStackFrame *stack,int sig,Signals::handler_func handler) {
	uint32_t *esp = (uint32_t*)stack->getSP();
	/* the ret-instruction of sigRet() should go to the old eip */
	*--esp = stack->getIP();
	/* save regs */
	*--esp = stack->getFlags();
	*--esp = stack->eax;
	*--esp = stack->ebx;
	*--esp = stack->ecx;
	*--esp = stack->edx;
	*--esp = stack->edi;
	*--esp = stack->esi;
	/* sigRet will remove the argument, restore the register,
	 * acknoledge the signal and return to eip */
	*--esp = t->getProc()->getSigRetAddr();
	stack->setIP((uintptr_t)handler);
	stack->setSP((uint32_t)esp);
	/* signal-number as argument */
	stack->eax = sig;
}

int UEnvBase::finishSignalHandler(IntrptStackFrame *stack) {
	uint32_t *esp = (uint32_t*)stack->getSP();
	if(!PageDir::isInUserSpace((uintptr_t)esp,9 * sizeof(uint32_t))) {
		Proc::segFault();
		/* never reached */
		assert(false);
	}
	/* restore regs */
	stack->esi = *esp++;
	stack->edi = *esp++;
	stack->edx = *esp++;
	stack->ecx = *esp++;
	stack->ebx = *esp++;
	stack->eax = *esp++;
	stack->setFlags(*esp++);
	/* return */
	stack->setIP(*esp++);
	stack->setSP((uintptr_t)esp);
	return 0;
}

bool UEnvBase::setupProc(int argc,const char *args,size_t argsSize,const ELF::StartupInfo *info,
                         uintptr_t entryPoint,int fd) {
	Thread *t = Thread::getRunning();
	IntrptStackFrame *frame = t->getIntrptStack();

	/*
	 * Initial stack:
	 * +------------------+  <- top
	 * |     arguments    |
	 * |        ...       |
	 * +------------------+
	 * |       argv       |
	 * +------------------+
	 * |       argc       |
	 * +------------------+
	 * |     TLSSize      |  0 if not present
	 * +------------------+
	 * |     TLSStart     |  0 if not present
	 * +------------------+
	 * |    entryPoint    |  0 for initial thread, thread-entrypoint for others
	 * +------------------+
	 */

	/* we need to know the total number of bytes we'll store on the stack */
	size_t totalSize = 0;
	if(argc > 0) {
		/* first round the size of the arguments up. then we need argc+1 pointer */
		totalSize += ROUND_UP(argsSize,sizeof(uint32_t));
		totalSize += sizeof(void*) * (argc + 1);
	}
	/* finally we need argc, argv, tlsSize, tlsStart and entryPoint */
	totalSize += sizeof(uint32_t) * 5;

	/* get esp */
	uint32_t *esp;
	t->getStackRange(NULL,(uintptr_t*)&esp,0);

	/* copy arguments on the user-stack (4byte space) */
	esp--;
	char **argv = NULL;
	if(argc > 0) {
		argv = (char**)(esp - argc);
		/* space for the argument-pointer */
		esp -= argc;
		/* start for the arguments */
		char *str = (char*)esp;
		for(int i = 0; i < argc; i++) {
			/* start <len> bytes backwards */
			size_t len = strlen(args) + 1;
			str -= len;
			/* store arg-pointer and copy arg */
			argv[i] = str;
			memcpy(str,args,len);
			/* to next */
			args += len;
		}
		/* ensure that we don't overwrites the characters */
		esp = (uint32_t*)(((uintptr_t)str & ~(sizeof(uint32_t) - 1)) - sizeof(uint32_t));
	}

	/* store argc and argv */
	*esp-- = (uintptr_t)argv;
	*esp-- = argc;
	/* add TLS args and entrypoint; use prog-entry here because its always the entry of the
	 * program, not the dynamic-linker */
	esp = UEnv::addArgs(t,esp,info->progEntry,false);

	/* if its the dynamic linker, give him the filedescriptor, so that he can load it including
	 * all shared libraries */
	if(info->linkerEntry != info->progEntry) {
		assert(fd != -1);
		*--esp = fd;
	}

	frame->setSP((uint32_t)esp);
	frame->ebp = frame->getSP();
	frame->setIP(entryPoint);
	return true;
}

void *UEnvBase::setupThread(const void *arg,uintptr_t tentryPoint) {
	Thread *t = Thread::getRunning();

	/*
	 * Initial stack:
	 * +------------------+  <- top
	 * |       arg        |
	 * +------------------+
	 * |     TLSSize      |  0 if not present
	 * +------------------+
	 * |     TLSStart     |  0 if not present
	 * +------------------+
	 * |    entryPoint    |  0 for initial thread, thread-entrypoint for others
	 * +------------------+
	 */

	/* get esp */
	uint32_t *esp;
	t->getStackRange(NULL,(uintptr_t*)&esp,0);

	/* put arg on stack */
	esp--;
	*esp-- = (uintptr_t)arg;
	/* add TLS args and entrypoint */
	return UEnv::addArgs(t,esp,tentryPoint,true);
}

uint32_t *UEnv::addArgs(Thread *t,uint32_t *esp,uintptr_t tentryPoint,bool newThread) {
	/* put address and size of the tls-region on the stack */
	uintptr_t tlsStart,tlsEnd;
	if(t->getTLSRange(&tlsStart,&tlsEnd)) {
		*esp-- = tlsEnd - tlsStart;
		*esp-- = tlsStart;
	}
	else {
		/* no tls */
		*esp-- = 0;
		*esp-- = 0;
	}

	*esp = newThread ? tentryPoint : 0;
	return esp;
}
