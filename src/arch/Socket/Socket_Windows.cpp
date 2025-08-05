#include "Socket_Windows.h"

#include <mutex>
#include <string>
#include <string_view>

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment (lib, "Ws2_32.lib")

namespace
{
	void LogWsaError(const char *operation)
	{
		printf("Windows Socket: %s failed with error: %d\n", operation, WSAGetLastError());
	}
}

class WindowsTcpClientSocket : public ClientSocket
{
public:
	WindowsTcpClientSocket(SOCKET socket):
		m_socket(socket)
	{}

	~WindowsTcpClientSocket()
	{
		if( shutdown(m_socket, SD_BOTH) == SOCKET_ERROR ) LogWsaError("shutdown");
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
	}

	virtual int Receive(char *buffer, int length, int timeoutMilliseconds = -1) override
	{
		if( timeoutMilliseconds >= 0 )
		{
			WSAPOLLFD readPollFd;
			readPollFd.fd = m_socket;
			readPollFd.events = POLLRDNORM;
			int pollResult = WSAPoll(&readPollFd, 1, timeoutMilliseconds);
			if( pollResult < 0 ) LogWsaError("WSAPoll");
			if( pollResult <= 0 ) return pollResult;
		}

		int recvResult = recv(m_socket, buffer, length, 0);
		if( recvResult < 0 ) LogWsaError("recv");
		// recv returns 0 if the connection is closed. Return -1 in that case
		// to differentiate it from a timeout.
		if( recvResult <= 0 ) return -1;
		return recvResult;
	}

	virtual int Send(const char *buffer, int length) override
	{
		int sendResult = send(m_socket, buffer, length, 0);
		if( sendResult < 0 ) LogWsaError("send");
		return sendResult;
	}

private:
	SOCKET m_socket;
};

class WindowsTcpServerSocket : public ServerSocket
{
public:
	WindowsTcpServerSocket(SOCKET socket):
		m_socket(socket)
	{}

	~WindowsTcpServerSocket()
	{
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
		WSACleanup();
	}

	virtual ClientSocket* Accept(int timeoutMilliseconds = -1) override
	{
		if( timeoutMilliseconds >= 0 )
		{
			fd_set readfds;
			FD_ZERO(&readfds);
			FD_SET(m_socket, &readfds);
			timeval timeout { timeoutMilliseconds / 1000, (timeoutMilliseconds % 1000) * 1000 };
			int selectResult = select(0, &readfds, nullptr, nullptr, &timeout);
			if( selectResult <= 0 ) return nullptr;
		}

		SOCKET clientSocket = accept(m_socket, nullptr, nullptr);
		if( clientSocket == INVALID_SOCKET ) return nullptr;

		return new WindowsTcpClientSocket(clientSocket);
	}

	static WindowsTcpServerSocket* Listen(const char *address)
	{
		SOCKET listenSocket = INVALID_SOCKET;
		struct addrinfo *addressInfo = nullptr;
		const char *operation = "";
		std::string_view addressView(address);
		size_t colonIndex = addressView.rfind(":");

		struct addrinfo hints = {};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;

		WSADATA wsaData;
		operation = "WSAStartup";
		int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if( wsaStartupResult != 0 ) goto error;

		operation = "getaddrinfo";
		// Try to split host and port.
		if( colonIndex == std::string_view::npos )
		{
			if( getaddrinfo(address, nullptr, &hints, &addressInfo) != 0 ) goto error;
		}
		else
		{
			std::string host(addressView.substr(0, colonIndex));
			std::string port(addressView.substr(colonIndex + 1));
			if( getaddrinfo(host.c_str(), port.c_str(), &hints, &addressInfo) != 0 ) goto error;
		}

		operation = "socket";
		listenSocket = socket(addressInfo->ai_family, addressInfo->ai_socktype, addressInfo->ai_protocol);
		if( listenSocket == INVALID_SOCKET ) goto error;

		operation = "bind";
		if( bind(listenSocket, addressInfo->ai_addr, (int)addressInfo->ai_addrlen) == SOCKET_ERROR ) goto error;

		freeaddrinfo(addressInfo);
		addressInfo = nullptr;

		operation = "listen";
		if( listen(listenSocket, 1) == SOCKET_ERROR ) goto error;

		return new WindowsTcpServerSocket(listenSocket);

	error:
		LogWsaError(operation);
		if( listenSocket != INVALID_SOCKET ) closesocket(listenSocket);
		if( addressInfo != nullptr ) freeaddrinfo(addressInfo);
		if( wsaStartupResult != 0 ) WSACleanup();
		return nullptr;
	}

private:
	SOCKET m_socket;
};

ServerSocket* ServerSocket::Listen(const char* address)
{
	return WindowsTcpServerSocket::Listen(address);
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
