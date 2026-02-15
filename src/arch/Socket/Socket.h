#ifndef SOCKET_H
#define SOCKET_H

#include <string>

class BroadcastSocket {
 public:
  virtual ~BroadcastSocket() {}

  /** Open a socket and start accepting broadcast packets. Caller must free
   * the returned BroadcastSocket. Listen is provided by the arch-specific
   * implementations.
   * @param address address to listen on
   * @return the broadcast socket, or nullptr on error
   */
  static BroadcastSocket* Listen(const char* address);

  /** Try to read data from the socket.
   * @param timeoutMilliseconds maximum time to wait for the data, or -1 to wait
   * indefinitely
   * @param buffer output buffer
   * @param length Maximum number of bytes to read. Must not be larger that the
   * size of the buffer.
   * @param clientAddress address where the data was received from
   * @param timeoutMilliseconds maximum time to wait for the data, or -1 to wait
   * indefinitely
   * @return number of bytes read, or -1 on error
   */
  virtual int Receive(
      char* buffer, int length, std::string& clientAddress,
      int timeoutMilliseconds = -1) = 0;

  /** Try to broadcast some data.
   * @param buffer input buffer
   * @param length length of the data
   * @return number of bytes written, or a negative number on error
   */
  virtual int Broadcast(const char* buffer, int length) = 0;

 protected:
  BroadcastSocket() {}
};

#endif

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
