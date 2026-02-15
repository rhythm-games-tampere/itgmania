#include <string>
#include <string_view>

#include "Socket.h"

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

namespace {
void LogWsaError(const char* operation) {
  printf(
      "Windows Socket: %s failed with error: %d\n", operation,
      WSAGetLastError());
}
}  // namespace

class WindowsUdpBroadcastSocket : public BroadcastSocket {
 public:
  WindowsUdpBroadcastSocket(SOCKET socket, USHORT port)
      : m_socket(socket), m_sendAddress{} {
    m_sendAddress.sin_family = AF_INET;
    m_sendAddress.sin_port = htons(port);
    m_sendAddress.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  }

  ~WindowsUdpBroadcastSocket() {
    closesocket(m_socket);
    m_socket = INVALID_SOCKET;
    WSACleanup();
  }

  int Receive(
      char* buffer, int length, std::string& clientAddress,
      int timeoutMilliseconds = -1) {
    clientAddress.clear();

    if (timeoutMilliseconds >= 0) {
      WSAPOLLFD readPollFd;
      readPollFd.fd = m_socket;
      readPollFd.events = POLLRDNORM;
      int pollResult = WSAPoll(&readPollFd, 1, timeoutMilliseconds);
      if (pollResult < 0) {
        LogWsaError("WSAPoll");
      }
      if (pollResult <= 0) {
        return pollResult;
      }
    }

    struct sockaddr_in receiveAddress{};
    int addressLength = sizeof(receiveAddress);
    int recvResult = recvfrom(
        m_socket, buffer, length, 0, (struct sockaddr*)&receiveAddress,
        &addressLength);
    if (recvResult < 0) {
      LogWsaError("recvfrom");
    }
    if (recvResult < 0) {
      return -1;
    }

    clientAddress.append(inet_ntoa(receiveAddress.sin_addr));
    clientAddress.append(":");
    clientAddress.append(std::to_string(ntohs(receiveAddress.sin_port)));

    return recvResult;
  }

  int Broadcast(const char* buffer, int length) {
    int sendResult = sendto(
        m_socket, buffer, length, 0, (const struct sockaddr*)&m_sendAddress,
        sizeof(m_sendAddress));
    if (sendResult < 0) {
      LogWsaError("sendto");
    }
    return sendResult;
  }

  static WindowsUdpBroadcastSocket* Listen(
      std::string_view host, std::string_view port) {
    SOCKET listenSocket = INVALID_SOCKET;
    struct addrinfo* addressInfo = nullptr;
    const char* operation = "";
    std::string hostString(host);
    std::string portString(port);
    USHORT portNumber = atoi(portString.c_str());
    BOOL yes = 1;

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE;

    WSADATA wsaData;
    operation = "WSAStartup";
    int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaStartupResult != 0) {
      goto error;
    }

    operation = "getaddrinfo";
    if (getaddrinfo(
            hostString.c_str(), portString.c_str(), &hints, &addressInfo) !=
        0) {
      goto error;
    }

    operation = "socket";
    listenSocket = socket(
        addressInfo->ai_family, addressInfo->ai_socktype,
        addressInfo->ai_protocol);
    if (listenSocket == INVALID_SOCKET) {
      goto error;
    }

    operation = "setsockopt";
    if (setsockopt(
            listenSocket, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes)) !=
        0) {
      goto error;
    }
    if (setsockopt(
            listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) !=
        0) {
      goto error;
    }

    operation = "bind";
    if (bind(
            listenSocket, addressInfo->ai_addr, (int)addressInfo->ai_addrlen) ==
        SOCKET_ERROR) {
      goto error;
    }

    freeaddrinfo(addressInfo);
    addressInfo = nullptr;

    return new WindowsUdpBroadcastSocket(listenSocket, portNumber);

  error:
    LogWsaError(operation);
    if (listenSocket != INVALID_SOCKET) {
      closesocket(listenSocket);
    }
    if (addressInfo != nullptr) {
      freeaddrinfo(addressInfo);
    }
    if (wsaStartupResult != 0) {
      WSACleanup();
    }
    return nullptr;
  }

 private:
  SOCKET m_socket;
  struct sockaddr_in m_sendAddress;
};

namespace {
std::pair<std::string_view, std::string_view> SplitToHostAndPort(
    std::string_view address) {
  size_t colonIndex = address.rfind(':');
  if (colonIndex == std::string_view::npos) {
    return std::make_pair(address, std::string_view{});
  }

  std::string_view host(address.substr(0, colonIndex));
  std::string_view port(address.substr(colonIndex + 1));
  return std::make_pair(host, port);
}
}  // namespace

BroadcastSocket* BroadcastSocket::Listen(const char* address) {
  auto hostAndPort = SplitToHostAndPort(address);
  return WindowsUdpBroadcastSocket::Listen(
      hostAndPort.first, hostAndPort.second);
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
