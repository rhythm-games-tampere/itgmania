#include "global.h"
#include "Socket.h"

int ClientSocket::ReceiveAll(char *buffer, int length)
{
	int remaining = length;
	while( remaining > 0 )
	{
		int receiveResult = Receive(buffer, remaining);
		if( receiveResult < 0 ) return -1;
		ASSERT( receiveResult <= remaining );
		buffer += receiveResult;
		remaining -= receiveResult;
	}
	return length;
}

int ClientSocket::SendAll(const char *buffer, int length)
{
	int remaining = length;
	while( remaining > 0 )
	{
		int sendResult = Send(buffer, remaining);
		if( sendResult < 0 ) return -1;
		ASSERT( sendResult <= remaining );
		buffer += sendResult;
		remaining -= sendResult;
	}
	return length;
}

/*
 * Copyright (C) 2025  Arttu Ylä-Outinen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
