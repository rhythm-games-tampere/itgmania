#include "Socket_Posix.h"

#include <netdb.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <errno.h>
#include <string>
#include <string_view>

namespace
{
	void LogSocketError(const char *operation, int errorCode = 0)
	{
		if( errorCode == 0 ) errorCode = errno;
		const char *message = operation == std::string("getaddrinfo")
			? gai_strerror(errorCode)
			: strerror(errorCode);
		printf("Posix socket: %s failed with error: %s (%d)\n", operation, message, errorCode);
	}

	int SelectForRead(int socketFd, int timeoutMilliseconds)
	{
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(socketFd, &fds);
		timeval timeout { timeoutMilliseconds / 1000, (timeoutMilliseconds % 1000) * 1000 };
		int selectResult = select(socketFd + 1, &fds, nullptr, nullptr, &timeout);
		if( selectResult < 0 ) LogSocketError("select");
		return selectResult;
	}
}

class PosixTcpClientSocket : public ClientSocket
{
public:
	PosixTcpClientSocket(int socketFd): m_socket(socketFd) {}

	~PosixTcpClientSocket()
	{
		if( shutdown(m_socket, SHUT_RDWR) < 0 ) LogSocketError("shutdown");
		close(m_socket);
		m_socket = -1;
	}

	virtual int Receive(char *buffer, int length, int timeoutMilliseconds = -1) override
	{
		if( timeoutMilliseconds >= 0 )
		{
			int selectResult = SelectForRead(m_socket, timeoutMilliseconds);
			if( selectResult <= 0 ) return selectResult;
		}

		int recvResult = recv(m_socket, buffer, length, 0);
		if( recvResult < 0 ) LogSocketError("recv");
		// recv returns 0 if the connection is closed. Return -1 in that case
		// to differentiate it from a timeout.
		if( recvResult <= 0 ) return -1;
		return recvResult;
	}

	virtual int Send(const char *buffer, int length) override
	{
		int sendResult = send(m_socket, buffer, length, 0);
		if( sendResult < 0 ) LogSocketError("send");
		return sendResult;
	}

private:
	int m_socket = -1;
};

class PosixTcpServerSocket : public ServerSocket
{
public:
	PosixTcpServerSocket(int socketFd): m_socket(socketFd) {}

	~PosixTcpServerSocket()
	{
		shutdown(m_socket, SHUT_RDWR);
		close(m_socket);
		m_socket = -1;
	}

	virtual ClientSocket* Accept(int timeoutMilliseconds = -1) override
	{
		if( timeoutMilliseconds >= 0 )
		{
			if( SelectForRead(m_socket, timeoutMilliseconds) <= 0 ) return nullptr;
		}

		int clientSocket = accept(m_socket, nullptr, nullptr);
		if( clientSocket < 0 ) return nullptr;

		return new PosixTcpClientSocket(clientSocket);
	}

	static PosixTcpServerSocket* Listen(const char *address)
	{
		int listenSocket = -1;
		struct addrinfo *addressInfo = nullptr;
		int errorCode = 0;
		const char *operation = "";
		std::string_view addressView(address);
		size_t colonIndex = addressView.rfind(":");

		struct addrinfo hints = {};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;

		operation = "getaddrinfo";
		// Try to split host and port.
		if( colonIndex == std::string_view::npos )
		{
			errorCode = getaddrinfo(address, nullptr, &hints, &addressInfo);
			if( errorCode != 0 ) goto error;
		}
		else
		{
			std::string host(addressView.substr(0, colonIndex));
			std::string port(addressView.substr(colonIndex + 1));
			errorCode = getaddrinfo(host.c_str(), port.c_str(), &hints, &addressInfo);
		}
		if( errorCode != 0 ) goto error;

		operation = "socket";
		listenSocket = socket(addressInfo->ai_family, addressInfo->ai_socktype, addressInfo->ai_protocol);
		if( listenSocket < 0 ) goto error;

		operation = "bind";
		if( bind(listenSocket, addressInfo->ai_addr, addressInfo->ai_addrlen) != 0) goto error;

		freeaddrinfo(addressInfo);
		addressInfo = nullptr;

		operation = "listen";
		if( listen(listenSocket, 1) != 0 ) goto error;

		return new PosixTcpServerSocket(listenSocket);

	error:
		LogSocketError(operation, errorCode);
		if( listenSocket != -1 )
		{
			shutdown(listenSocket, SHUT_RDWR);
			close(listenSocket);
		}
		if( addressInfo != nullptr ) freeaddrinfo(addressInfo);
		return nullptr;
	}

private:
	int m_socket = -1;
};

ServerSocket* ServerSocket::Listen(const char* address)
{
	return PosixTcpServerSocket::Listen(address);
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
