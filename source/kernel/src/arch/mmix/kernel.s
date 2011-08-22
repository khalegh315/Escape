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

#===========================================
# Imports / Exports
#===========================================

	.global	_bcode
	.global	_bdata
	.global	_bbss

	.extern	bspstart
	.extern intrpt_forcedTrap
	.extern intrpt_dynTrap

	.global start

	.global logByte
	.global debugc
	.global paging_setrV

	.global tc_clear
	.global tc_update

	.global cpu_getSyscallNo
	.global cpu_getFaultLoc
	.global cpu_syncid
	.global cpu_rdtsc
	.global cpu_getGlobal
	.global cpu_getSpecial
	.global cpu_getKSpecials
	.global cpu_setKSpecials

	.global thread_startup
	.global thread_idle
	.global thread_initSave
	.global	thread_doSwitchTo

	.global stackCopy
	.global stackCopySize

#===========================================
# Constants
#===========================================

	.set	PAGE_SIZE,0x2000
	.set	STACK_SIZE,PAGE_SIZE

#===========================================
# Start
#===========================================

.section .text
_bcode:

start:
	# copy the current stack to stackCopy
	GETA	$1,stackCopy
	SET		$2,$0
	SET		$3,$2
	ANDNL	$3,#1FFF
	GETA	$4,stackCopySize
	SUBU	$5,$2,$3
	ADDU	$5,$5,8
	STOU	$5,$4,0
1:
	LDOU	$4,$3,0
	STOU	$4,$1,0
	ADDU	$1,$1,8
	ADDU	$3,$3,8
2:
	CMP		$4,$2,$3
	BNN		$4,1b

	# establish the register-stack
	UNSAVE	0,$0
	# set rSS; we need it when cloning in the test-kernel, for example
	GET		$1,rS
	PUT		rSS,$1

	# store rG for kernel; we'll have to set it in the trap-handlers (the user may change it)
	GETA	$1,krg
	GET		$2,rG
	STOU	$2,$1,0

	# bootinfo is in $0
	LDOU	$254,$0,32						# load kstackbegin from bootinfo
	INCL	$254,STACK_SIZE
	SUBU	$254,$254,8						# stack-pointer = kstackbegin + STACK_SIZE - 8
	OR		$253,$254,$254					# frame-pointer = stack-pointer

	# enable keyboard-interrupts
	SETH	$1,#8006
	STCO	#2,$1,0

	# call main
	OR		$1,$0,$0						# pass bootinfo to main
	SET		$2,$254							# pass pointer to stackBegin, which is set in main
	SUBU	$3,$254,8						# pass pointer to rss, which is set in main
	PUSHJ	$0,bspstart						# call 'bspstart' function

	# setup forced and dynamic trap handlers
	GETA	$1,forcedTrap
	PUT		rT,$1
	GETA	$1,dynamicTrap
	PUT		rTT,$1

	# go to user mode (initloader)
	PUT		rWW,$0							# main returned the entry-point
	SETH	$0,#8000
	PUT		rXX,$0							# skip to rWW
	SUBU	$0,$254,8
	LDOU	$0,$0,0
	PUT		rSS,$0							# set kernel-stack
	LDOU	$0,$254,0						# pointer to stackBegin
	UNSAVE	0,$0							# setup initial stack for initloader
	NEG		$255,0,1						# set rK = -1
	RESUME	1

	# just to be sure
loop:
	JMP		loop

#===========================================
# Traps
#===========================================

forcedTrap:
	SAVE	$255,1
	# set rG
	GETA	$0,krg
	LDOU	$0,$0,0
	PUT		rG,$0
	# setup sp and fp
	GET		$0,rSS
	INCL	$0,STACK_SIZE
	SUBU	$254,$0,8
	SET		$253,$254
	SETMH	$0,#FE
	PUT		rK,$0							# enable exception-bits for kernel (to see errors early)
	SET		$0,$255							# save $255
	GET		$2,rS
	PUSHJ	$1,intrpt_forcedTrap
	UNSAVE	1,$0
	PUT		rSS,$255						# after cloning, the new value for rSS will be in $255
	NEG		$255,0,1						# rK = -1
	RESUME	1

dynamicTrap:
	SAVE	$255,1
	# set rG
	GETA	$0,krg
	LDOU	$0,$0,0
	PUT		rG,$0
	# setup sp and fp
	GET		$0,rSS
	INCL	$0,STACK_SIZE
	SUBU	$254,$0,8
	SET		$253,$254
	SUBU	$254,$254,6*8					# determine irq-number
	GET		$0,rQ
	SUBU	$1,$0,1
	SADD	$3,$1,$0
	ANDN	$1,$0,$1
	ANDN	$0,$0,$1
	# ensure that we don't trigger an exception by setting rK. that means, we'll always report the
	# right-most exception bit and ignore all others. but this is sufficient in our case, because
	# all the fatal exceptions cause a kill anyway and for the protection-faults we can figure out
	# what we have to do by ourself anyway.
	SETMH	$1,#FE
	ANDN	$0,$0,$1
	PUT		rQ,$0							# remove that bit from rQ
	PUT		rK,$1							# enable exception-bits for kernel (to see errors early)
	# call handler with rS as argument
	GET		$2,rS
	SET		$0,$255							# save $255
	PUSHJ	$1,intrpt_dynTrap				# call intrpt_dynTrap(rS,irqNo)
	# restore context
	UNSAVE	1,$0
	GET		$255,rWW						# check whether rWW is negative
	BNN		$255,1f							# if not, set rK to -1
	NEG		$255,0,1
	ANDNMH	$255,#0001						# if so, set it to -1, but clear privileged pc bit
	RESUME	1								# this is only required for the idle-thread
1:
	NEG		$255,0,1						# rK = -1
	RESUME	1

#===========================================
# Thread
#===========================================

thread_startup:
	# $254+0  = proc entry-point
	# $254+8  = argument
	# $254+16 = thread entry-point
	LDOU	$0,$254,16
	SETH	$3,#8000
	CMPU	$4,$0,$3
	BN		$4,2f
	# stay in kernel
	PUSHGO	$0,$0,0
1:
	JMP		1b

2:
	# setup user-env
	LDOU	$2,$254,8
	LDOU	$3,$254,16
	PUSHJ	$1,uenv_setupThread				# call uenv_setupThread(arg,entryPoint)
	LDOU	$0,$254,0						# proc entry-point
	SET		$254,$1							# set fp and sp
	SET		$253,$1

	# go to user mode (initloader)
	PUT		rWW,$0							# entry-point
	SETH	$0,#8000
	PUT		rXX,$0							# skip to rWW
	NEG		$255,0,1						# set rK = -1
	RESUME	1

# void thread_idle(void)
thread_idle:
	NEG		$0,0,1
	ANDNMH	$0,#0001
	PUT		rK,$0							# enable all bits, except privileged pc
1:
	SYNC	4								# power-saver mode
	JMP		1b

# int thread_initSave(sThreadRegs *saveArea,void *newStack)
thread_initSave:
	SET		$251,$0
	SET		$252,$1							# save $0 and $1, SAVE will set rL to 0
	PUT		rL,0							# remove $0, we don't need it anymore
	SAVE	$255,0							# save the state on the current stack
	# copy the register-stack
	GET		$0,rSS							# stack-begin
	GET		$1,rS							# stack-end
	SUBU	$1,$1,$0						# number of bytes to copy
1:
	LDOU	$2,$0,$1
	STOU	$2,$252,$1
	SUBU	$1,$1,8
	BNN		$1,1b
	# now copy the software-stack
	INCL	$0,PAGE_SIZE-8
	INCL	$252,PAGE_SIZE-8
	SUBU	$1,$0,$254						# number of bytes to copy
	SUBU	$0,$0,$1
	SUBU	$252,$252,$1
1:
	LDOU	$2,$0,$1
	STOU	$2,$252,$1
	SUBU	$1,$1,8
	BNN		$1,1b
	# store data to save-area
	STOU	$255,$251,0						# store stack-end
	# now unsave, to be able to continue with the current stack
	UNSAVE	0,$255
	SET		$0,0							# return 0
	POP		1,0

# int thread_doSwitchTo(sThreadRegs *oldArea,sThreadRegs *newArea,tPageDir pdir,tid_t tid)
thread_doSwitchTo:
	PUT		rF,$3							# just for backtracing: put the thread-id in rF
	SET		$250,$0							# save $0, SAVE will set rL to 0
	SET		$251,$1
	SET		$252,$2
	PUT		rL,0							# remove $0, we don't need it anymore
	# save state of old thread
	SAVE	$255,0
	STOU	$255,$250,0						# store stack-end
	# restore state of new thread
	LDOU	$0,$252,8						# load rV
	PUT		rV,$0
	LDOU	$0,$251,0						# load stackend
	SET		$1,$0
	ANDNL	$1,#1FFF						# to page-begin
	PUT		rSS,$1							# set rSS
	UNSAVE	0,$0
	SET		$0,1							# return 1
	POP		1,0

#===========================================
# Paging
#===========================================

# void paging_setrV(uint64_t rv)
paging_setrV:
	PUT		rV,$0
	POP		0,0

#===========================================
# TC
#===========================================

# void tc_clear(void)
tc_clear:
	SYNC	6
	POP		0,0

# void tc_update(uint64_t key)
tc_update:
	LDVTS	$0,$0,0
	POP		0,0

#===========================================
# CPU
#===========================================

# int cpu_getSyscallNo(void)
cpu_getSyscallNo:
	GET		$0,rXX
	SRU		$0,$0,8
	AND		$0,$0,0xFF
	POP		1,0

# uintptr_t cpu_getFaultLoc(void)
cpu_getFaultLoc:
	GET		$0,rYY
	POP		1,0

# void cpu_syncid(uintptr_t start,uintptr_t end)
cpu_syncid:
	SETL	$3,#100
1:
	SYNCD	#FF,$0,0
	SYNCID	#FF,$0,0
	ADDU	$0,$0,$3
	CMPU	$2,$0,$1
	BN		$2,1b
	POP		0,0

# uint64_t cpu_rdtsc(void)
cpu_rdtsc:
	GET		$0,rC
	POP		1,0

# uint64_t cpu_getGlobal(int rno)
cpu_getGlobal:
	SETH	$1,#0000
	ORML	$1,#C100
	SLU		$0,$0,8
	OR		$1,$1,$0						# ORI $0,$<rno>,0
	PUT		rX,$1
	GETA	$0,@+12
	PUT		rW,$0
	RESUME	0
	POP		1,0

# uint64_t cpu_getSpecial(int rno)
cpu_getSpecial:
	SETH	$1,#0000
	ORML	$1,#FE00
	OR		$1,$1,$0						# GET $0,<rno>
	PUT		rX,$1
	GETA	$0,@+12
	PUT		rW,$0
	RESUME	0
	POP		1,0

# void cpu_getKSpecials(uint64_t *rbb,uint64_t *rww,uint64_t *rxx,uint64_t *ryy,uint64_t *rzz)
cpu_getKSpecials:
	GET		$5,rBB
	STOU	$5,$0,0
	GET		$5,rWW
	STOU	$5,$1,0
	GET		$5,rXX
	STOU	$5,$2,0
	GET		$5,rYY
	STOU	$5,$3,0
	GET		$5,rZZ
	STOU	$5,$4,0
	POP		0,0

# void cpu_setKSpecials(uint64_t rbb,uint64_t rww,uint64_t rxx,uint64_t ryy,uint64_t rzz)
cpu_setKSpecials:
	PUT		rBB,$0
	PUT		rWW,$1
	PUT		rXX,$2
	PUT		rYY,$3
	PUT		rZZ,$4
	POP		0,0

#===========================================
# Input/Output
#===========================================

# void debugc(char character)
debugc:
	GET		$1,rJ
	SETH	$2,#8002						# base address: #8002000000000000
	CMPU	$3,$0,0xA						# char = \n?
	BNZ		$3,1f
	SET		$4,0xD
	PUSHJ	$3,debugc						# putc('\r')
1:
	LDOU	$3,$2,#10						# read ctrl-reg
	AND		$3,$3,#1						# exract RDY-bit
	PBZ		$3,1b							# wait until its set
	STOU	$0,$2,#18						# write char
	PUT		rJ,$1
	POP		0,0

	LOC		@+(8-@)&7
krg:
	OCTA	0

#===========================================
# Data
#===========================================

.section .data
_bdata:

.section .bss
_bbss:

# one page for the stack-copy; this is used for starting threads (to give them their initial
# kernelstack)
stackCopy:
	OCTA	0
	LOC		@+8192-8*3
	OCTA	0
stackCopySize:
	OCTA	0
