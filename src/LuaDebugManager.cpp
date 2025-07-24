#include "global.h"
#include "LuaDebugManager.h"
#include "LuaDebugger.h"
#include "LuaDebugDap.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "arch/Socket/Socket.h"

#include <climits>
#include <mutex>
#include <string_view>

LuaDebugManager *LUADEBUG = nullptr;

namespace
{
	const char *const DefaultAddress = "localhost:8173";
}

class LuaDebugManager::Impl
{
public:

	Impl()
	{
		m_serverThread.SetName("LuaDebug");
	}

	~Impl()
	{
		Stop();
	}

	void Start(std::string address, bool startPaused)
	{
		if( m_serverRunning ) return;
		std::unique_lock lock(m_serverMutex);
		if( m_serverRunning ) return;
		m_serverRunning = true;

		m_startPaused = startPaused;
		m_serverAddress = address.empty() ? DefaultAddress : address;
		m_serverThread.Create(ServerThread_Start, this);
	}

	void Stop()
	{
		if( !m_serverRunning ) return;
		std::unique_lock lock(m_serverMutex);
		if( !m_serverRunning ) return;
		m_serverRunning = false;
		m_serverThread.Wait();
	}

	void ActivateState(lua_State *lua) { m_debugger.ActivateState(lua); }
	void DeactivateState(lua_State *lua) { m_debugger.DeactivateState(lua); }

private:
	std::mutex m_serverMutex;
	RageThread m_serverThread;
	std::string m_serverAddress;
	bool m_startPaused = false;
	bool m_serverRunning = false;
	LuaDebug::Debugger m_debugger;

	bool SendMessage(ClientSocket &clientSocket, LuaDebug::Message& message)
	{
		std::string body = Json::FastWriter{}.write(message);
		std::string data =
			std::string("Content-Length: ") +
			std::to_string(body.length()) +
			"\r\n\r\n" +
			body;
		return clientSocket.SendAll(data.c_str(), data.length()) == (int)data.length();
	}

	void Serve(ClientSocket &clientSocket)
	{
		uint64_t nextMessageSeq = 1;
		size_t bufferLength = 1024;
		size_t bufferFilled = 0;
		std::unique_ptr<char[]> buffer(new char[bufferLength]);

		while( m_serverRunning )
		{
			LuaDebug::Message response;
			while( m_debugger.TryReceiveResponse(response) )
			{
				response["seq"] = nextMessageSeq++;
				if( !SendMessage(clientSocket, response) ) return;
				if( response["type"] == "response" &&
					response["command"] == "disconnect" ) return;
			}

			ASSERT( bufferFilled <= bufferLength );
			if( std::string_view(buffer.get(), bufferFilled).find("\r\n\r\n") == std::string_view::npos )
			{
				// We don't have a complete header; need to read more data.
				size_t freeSpace = bufferLength - bufferFilled;
				if( freeSpace == 0 )
				{
					LOG->Warn("LuaDebugger: invalid request header");
					return;
				}
				ASSERT(freeSpace <= INT_MAX);
				int recvResult = clientSocket.Receive(buffer.get() + bufferFilled, freeSpace, 50);
				if( recvResult < 0 ) return;
				ASSERT( recvResult <= (int)freeSpace );
				bufferFilled += recvResult;

				if( std::string_view(buffer.get(), bufferFilled).find("\r\n\r\n") == std::string_view::npos )
					continue;
			}

			LuaDebug::Header header;
			if( !header.TryParse(buffer.get(), bufferFilled) )
			{
				LOG->Warn("LuaDebugger: invalid request header");
				return;
			}
			ASSERT( header.GetHeaderLength() <= bufferFilled );

			size_t totalSize = header.GetHeaderLength() + header.GetContentLength();
			if( totalSize > 10000000 )
			{
				LOG->Warn("LuaDebugger: too big request");
				return;
			}

			if( bufferLength < totalSize )
			{
				bufferLength = totalSize;
				std::unique_ptr<char[]> newBuffer(new char[bufferLength]);
				memcpy(newBuffer.get(), buffer.get(), bufferFilled);
				buffer = std::move(newBuffer);
			}

			if( totalSize > bufferFilled )
			{
				size_t bytesMissing = totalSize - bufferFilled;
				ASSERT( bytesMissing <= INT_MAX );
				if( clientSocket.ReceiveAll(buffer.get() + bufferFilled, bytesMissing) != (int)bytesMissing )
					return;
				bufferFilled += bytesMissing;
			}

			LuaDebug::Request request;
			if( !request.TryParse(buffer.get() + header.GetHeaderLength(), bufferFilled - header.GetHeaderLength()) )
			{
				LOG->Warn("LuaDebugger: invalid request body");
				return;
			}

			m_debugger.SendRequest(std::move(request));

			// Move rest of the data to the start of the buffer.
			if( bufferFilled > totalSize )
			{
				memcpy(buffer.get(), buffer.get() + totalSize, bufferFilled - totalSize);
			}
			bufferFilled -= totalSize;
		}

		// Report to the client that the server is terminating.
		LuaDebug::Event terminatedEvent = LuaDebug::CreateTerminatedEvent();
		terminatedEvent["seq"] = nextMessageSeq++;
		SendMessage(clientSocket, terminatedEvent);
	}

	int ServerThread()
	{
		std::unique_ptr<ServerSocket> serverSocket(ServerSocket::Listen(m_serverAddress.c_str()));
		if( !serverSocket ) return -1;

		if( m_startPaused ) m_debugger.SendRequest(LuaDebug::CreatePauseRequest(0));

		while( m_serverRunning )
		{
			std::unique_ptr<ClientSocket> clientSocket(serverSocket->Accept(500));
			if( !clientSocket ) continue;
			LOG->Info("LuaDebugger: client connected");
			Serve(*clientSocket.get());
			// Send a fake disconnect request in case the connection was closed unexpectedly.
			m_debugger.SendRequest(LuaDebug::CreateDisconnectRequest(true));
			LOG->Info("LuaDebugger: client disconnected");
		}

		// Resume execution with a fake disconnect request before terminating
		// the server thread.
		m_debugger.SendRequest(LuaDebug::CreateDisconnectRequest(false));
		return 0;
	}

	static int ServerThread_Start(void *data)
	{
		return ((Impl*)data)->ServerThread();
	}
};

LuaDebugManager::LuaDebugManager(): m_impl(std::make_unique<LuaDebugManager::Impl>()) {}
LuaDebugManager::~LuaDebugManager() = default;

void LuaDebugManager::Start(std::string address, bool startPaused) { m_impl->Start(address, startPaused); }

void LuaDebugManager::Stop() { m_impl->Stop(); }

void LuaDebugManager::ActivateState(lua_State *lua)
{
	if( LUADEBUG != nullptr ) LUADEBUG->m_impl->ActivateState(lua);
}

void LuaDebugManager::DeactivateState(lua_State *lua)
{
	if( LUADEBUG != nullptr ) LUADEBUG->m_impl->DeactivateState(lua);
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
