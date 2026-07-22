#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

/**
 * Simple TCP client wrapper for sending JSON messages to Rust backend
 * Connects to 127.0.0.1:1337 and keeps connection alive
 * Includes automatic reconnection on failure
 */
class TcpClient {
public:
	TcpClient(const std::string& host = "127.0.0.1", int port = 1337);
	~TcpClient();

	/**
	 * Connects to the TCP server
	 * Retries internally on failure
	 */
	bool Connect();

	/**
	 * Sends a JSON message string to the server
	 * Automatically reconnects if connection is lost
	 * Returns true on success, false on failure
	 */
	bool SendMessage(const std::string& jsonPayload);

	/**
	 * Disconnects from server
	 */
	void Disconnect();

	/**
	 * Returns true if connected and ready to send
	 */
	bool IsConnected() const;

private:
	SOCKET m_socket;
	std::string m_host;
	int m_port;
	mutable std::mutex m_lock;
	bool m_connected;

	/**
	 * Internal method to establish socket connection
	 */
	bool ConnectInternal();

	/**
	 * Close current socket
	 */
	void DisconnectInternal();
};

typedef std::shared_ptr<TcpClient> TcpClientPtr;
