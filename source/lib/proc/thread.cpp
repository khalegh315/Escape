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

#include <proc/thread.h>
#include <fstream>
#include <file.h>

namespace proc {
	vector<thread*> thread::get_list() {
		vector<thread*> threads;
		file dir("/system/processes");
		vector<sDirEntry> files = dir.list_files(false);
		for(vector<sDirEntry>::const_iterator it = files.begin(); it != files.end(); ++it) {
			try {
				string threadDir(string("/system/processes/") + it->name + "/threads");
				file tdir(threadDir);
				vector<sDirEntry> tfiles = tdir.list_files(false);
				for(vector<sDirEntry>::const_iterator tit = tfiles.begin(); tit != tfiles.end(); ++tit) {
					string tpath = threadDir + "/" + tit->name + "/info";
					ifstream tis(tpath.c_str());
					thread* t = new thread();
					tis >> *t;
					threads.push_back(t);
				}
			}
			catch(const io_exception&) {
			}
		}
		return threads;
	}

	thread *thread::get_thread(pid_t pid,tid_t tid) {
		char tname[12], pname[12];
		itoa(tname,sizeof(tname),tid);
		itoa(pname,sizeof(pname),pid);
		string tpath = string("/system/processes/") + pname + "/threads/" + tname + "/info";
		ifstream tis(tpath.c_str());
		thread* t = new thread();
		tis >> *t;
		return t;
	}

	void thread::clone(const thread& t) {
		_tid = t._tid;
		_pid = t._pid;
		_procName = t._procName;
		_state = t._state;
		_flags = t._flags;
		_prio = t._prio;
		_stackPages = t._stackPages;
		_schedCount = t._schedCount;
		_syscalls = t._syscalls;
		_cycles = t._cycles;
		_runtime = t._runtime;
		_cpu = t._cpu;
	}

	std::istream& operator >>(std::istream& is,thread& t) {
		std::istream::size_type unlimited = std::numeric_limits<streamsize>::max();
		is.ignore(unlimited,' ') >> t._tid;
		is.ignore(unlimited,' ') >> t._pid;
		is.ignore(unlimited,' ') >> t._procName;
		// the process name might be "name" or "name arg1 arg2". so, check if the last read char
		// was already the newline. if not, skip everything until the next newline.
		is.unget();
		if(is.peek() != '\n')
			is.ignore(unlimited,'\n');
		is.ignore(unlimited,' ') >> t._state;
		is.ignore(unlimited,' ') >> t._flags;
		is.ignore(unlimited,' ') >> t._prio;
		is.ignore(unlimited,' ') >> t._stackPages;
		is.ignore(unlimited,' ') >> t._schedCount;
		is.ignore(unlimited,' ') >> t._syscalls;
		is.ignore(unlimited,' ') >> t._runtime;
		is.setf(istream::hex);
		is.ignore(unlimited,' ') >> t._cycles;
		is.setf(istream::dec);
		is.ignore(unlimited,' ') >> t._cpu;
		return is;
	}

	std::ostream& operator <<(std::ostream& os,thread& t) {
		os << "thread[" << t.tid() << " in " << t.pid() << ":" << t.procName() << "]:\n";
		os << "\tflags     : " << t.flags() << "\n";
		os << "\tstate     : " << t.state() << "\n";
		os << "\tpriority  : " << t.prio() << "\n";
		os << "\tstackPages: " << t.stackPages() << "\n";
		os << "\tschedCount: " << t.schedCount() << "\n";
		os << "\tsyscalls  : " << t.syscalls() << "\n";
		os << "\tcycles    : " << t.cycles() << "\n";
		os << "\truntime   : " << t.runtime() << "\n";
		os << "\tlastCPU   : " << t.cpu() << "\n";
		return os;
	}
}
