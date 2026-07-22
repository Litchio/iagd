#include "stdafx.h"
#include "TcpClient.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <thread>
#include "Logger.h"

#pragma comment(lib, "ws2_32.lib")

TcpClient::TcpClient(const std::string& host, int port)
	: m_socket(INVALID_SOCKET), m_host(host), m_port(port), m_connected(false) {
	// Initialize Winsock
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		LogToFile(LogLevel::WARNING, L"WSAStartup failed with error code: " + std::to_wstring(iResult));
	}
}

TcpClient::~TcpClient() {
	Disconnect();
	WSACleanup();
}

bool TcpClient::Connect() {
	std::lock_guard<std::mutex> guard(m_lock);
	return ConnectInternal();
}

bool TcpClient::ConnectInternal() {
	if (m_connected && m_socket != INVALID_SOCKET) {
		return true;
	}

	// Create socket
	m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_socket == INVALID_SOCKET) {
		LogToFile(LogLevel::WARNING, L"TCP socket() failed: " + std::to_wstring(WSAGetLastError()));
		return false;
	}

	// Set socket timeout to 5 seconds (5000 ms)
	int timeout = 5000;
	setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
	setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

	// Resolve server address
	sockaddr_in serverAddr = {};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(m_port);

	int iResult = inet_pton(AF_INET, m_host.c_str(), &serverAddr.sin_addr);
	if (iResult <= 0) {
		LogToFile(LogLevel::WARNING, L"TCP inet_pton() failed for host: " + std::wstring(m_host.begin(), m_host.end()));
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
		return false;
	}

	// Connect to server
	iResult = connect(m_socket, (sockaddr*)&serverAddr, sizeof(serverAddr));
	if (iResult == SOCKET_ERROR) {
		int err = WSAGetLastError();
		LogToFile(LogLevel::WARNING, L"TCP connect() failed: " + std::to_wstring(err));
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
		return false;
	}

	m_connected = true;
	LogToFile(LogLevel::INFO, L"TCP connection established to 127.0.0.1:1337");
	return true;
}

bool TcpClient::SendMessage(const std::string& jsonPayload) {
	std::lock_guard<std::mutex> guard(m_lock);

	// Try to connect if not already connected
	if (!m_connected || m_socket == INVALID_SOCKET) {
		if (!ConnectInternal()) {
			LogToFile(LogLevel::WARNING, L"TCP SendMessage: Failed to connect before sending");
			return false;
		}
	}

	// Add newline terminator for the Rust server to parse line-by-line
	std::string message = jsonPayload + "\n";

	// Send the message
	int iResult = send(m_socket, message.c_str(), (int)message.length(), 0);
	if (iResult == SOCKET_ERROR) {
		int err = WSAGetLastError();
		LogToFile(LogLevel::WARNING, L"TCP send() failed: " + std::to_wstring(err));
		DisconnectInternal();
		return false;
	}

	return true;
}

void TcpClient::Disconnect() {
	std::lock_guard<std::mutex> guard(m_lock);
	DisconnectInternal();
}

void TcpClient::DisconnectInternal() {
	if (m_socket != INVALID_SOCKET) {
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
	}
	m_connected = false;
}

bool TcpClient::IsConnected() const {
	std::lock_guard<std::mutex> guard(m_lock);
	return m_connected && m_socket != INVALID_SOCKET;
}
