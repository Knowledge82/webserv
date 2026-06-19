
#include <iostream>
#include <sys/socket.h> // socket()
#include <unistd.h> // close()
#include <arpa/inet.h> // IP/port helper functions
#include <cstring> // memset()
#include <string>

int	main(void) {
	// 1. creating a single socket
	int	server_fd = socket(AF_INET, SOCK_STREAM, 0);
						// __domain: AF_INET/6: IPv4/ipv6
						// __type: SOCK_STREAM: tcp
						// __value: 0
	if (server_fd == -1) {
		std::cerr << "socket failure" << std::endl;
		return 1;
	}

	// 1.2. prepare socket for reusability
	int	yes = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
		std::cerr << "bind failed" << std::endl;
		close(server_fd);
		return 1;
	}

	// 2. Prepare address
	sockaddr_in	address;
	std::memset(&address, 0, sizeof(address));

	address.sin_family = AF_INET;
	address.sin_port = htons(8080);
	address.sin_addr.s_addr = inet_addr("127.0.0.1");

	// 3. attach socket to the address
	if (bind(server_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == -1) {
						// server_fd is the fd
						// bind expects a generic sockaddr pointer, so we cast
							// ipv4 to that type
						// we tell how big the address data is
		std::cerr << "bind failed" << std::endl;
		close(server_fd);
		return 1;
	}

	// 4. make the socket listen incoming connections
	if (listen(server_fd, 10) == -1) { // at this point listen becomes server socket
						// server_fd: self-ex
						// backlog: 10 for default value,
							// 128 common real world value
							// SOMAXCONN system maximum
		std::cerr << "listen failed" << std::endl;
		close(server_fd);
		return 1;
	}

	std::cout << "Listening on 127.0.0.1:8080" << std::endl;

	// 5. Accept connections
	int	client_fd = accept(server_fd, NULL, NULL);
	if (client_fd == -1) {
		std::cerr << "accept failed" << std::endl;
		close(server_fd);
		return 1;
	}

	// 6. read what the client sends
	char	buffer[1024];
	ssize_t	bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
	if (bytes_received == -1) {
		std::cerr << "recv failed\n";
		close(client_fd);
		close(server_fd);
		return 1;
	}
	std::cout << "Client connected" << std::endl;
	std::string	request(buffer, bytes_received);
	std::cout << request;

	// 7. creating response

	const std::string response =
    "HTTP/1.1 200 OK\r\n"				// status line
    "Content-Length: 5\r\n"				// body lenght for hello
    "Content-Type: text/plain\r\n"		// content-type of body: text
    "\r\n"								// empty line to separate headers
    "Hello";							// body

	// 8.route response to client
	ssize_t	bytes_sent = send(client_fd, response.c_str(), response.size(), 0);
	if (bytes_sent == -1)
	{
		std::cerr << "send failed\n";
		close(client_fd);
		close(server_fd);
		return 1;
	}

	// EXIT 1: close connection
	close(client_fd);
	close(server_fd);
	return 0;
}
