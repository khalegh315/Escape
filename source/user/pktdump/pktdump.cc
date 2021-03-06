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

#include <esc/proto/nic.h>
#include <esc/proto/socket.h>
#include <esc/stream/std.h>

using namespace esc;

struct EthernetHeader {
	NIC::MAC dst;
	NIC::MAC src;
	uint16_t type;
};

int main() {
	ulong buffer[1024];
	Socket sock(Socket::SOCK_RAW_ETHER,Socket::PROTO_ANY);
	while(1) {
		esc::Socket::Addr addr;
		size_t res = sock.recvfrom(addr,buffer,sizeof(buffer));

		EthernetHeader *ether = reinterpret_cast<EthernetHeader*>(buffer);
		esc::sout << "Got packet:\n";
		esc::sout << " src  = " << ether->src << "\n";
		esc::sout << " dst  = " << ether->dst << "\n";
		esc::sout << " type = " << ether->type << "\n";
		esc::sout << " len  = " << res << "\n\n";
		esc::sout.flush();
	}
	return 0;
}
