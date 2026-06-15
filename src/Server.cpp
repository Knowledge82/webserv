/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 15:18:22 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/11 15:27:49 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
#include "FdUtils.hpp"
#include "Log.hpp"

#include <poll.h>
#include <stdexcept>
#include <cstring>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

Server::Server(const Config &cfg)
	: cfg_(cfg)
	, listenFds_()
	, connections_()
	, listenFdToServerIndex_()
	, pollFds_()
	, fdEntries_()
{
	setupListenSockets();
}

static int	createListenSocket(const std::string &host, int port)
{
	int	listenFd = ::socket(AF_INET, SOCK_STREAM, 0);//socket() asks the kernel to create a struct socket
													 //and return an fd.
	//Important: socket() alone does not open a port or "listen".
	//It just creates a template: type is set, but no address or port yet.
	
	// AF_INET = IPv4 (Address Family Internet),
	// alternatives: AF_INET6 (IPv6), AF_UNIX (local sockets via file)
	// SOCK_STREAM = stream socket, meaning TCP: reliable delivery, guaranteed ordering,
	// data flows as a byte stream without message boundaries. Alternative: SOCK_DGRAM for UDP.
	// 0 = default protocol (for AF_INET + SOCK_STREAM it's TCP). Could use IPPROTO_TCP explicitly.
	if (listenFd < 0)
		throw std::runtime_error("socket failed");

/* When the server stops, TCP connections don't die instantly.
   They enter TIME_WAIT state for ~2 minutes (protection against stray network packets).
   During this time the kernel considers the port busy. bind() will return EADDRINUSE.
   SO_REUSEADDR tells the kernel: allow reusing the address/port even if connections
   are still in TIME_WAIT. Essential for development,
   otherwise you'd wait 2 minutes after every restart.

	SOL_SOCKET — socket-level option (not TCP/IP specific)
	SO_REUSEADDR — the option itself
	&yes — pointer to value (int yes = 1 - enable)
	sizeof(yes) — value size */
	int	yes = 1;
	::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	setNonBlocking(listenFd);


/* sockaddr_in — IPv4 address structure:
	cppstruct sockaddr_in
	{
		sa_family_t    sin_family;  // AF_INET
		in_port_t      sin_port;    // port (network byte order!)
		struct in_addr sin_addr;    // IP address
		char           sin_zero[8]; // padding, alignment
	}; 		*/
	struct sockaddr_in	addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
/* This is one of the most important concepts in network programming — byte order.
Numbers in your x86/x64 processor memory are stored in little-endian: least significant byte first.
Port 8080 in hex = 0x1F90. In x86 memory: 90 1F.
The network uses big-endian (network byte order): most significant byte first: 1F 90.
If you pass the port without conversion — the kernel interprets 0x1F90 as 0x901F = port 36895.
The server starts on the wrong port.
htons = Host To Network Short (2 bytes). Swaps bytes if needed. */
	addr.sin_port = htons(static_cast<unsigned short>(port));
	if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)//Converts "0.0.0.0" or "192.168.1.1" to binary 32-bit IPv4 address in network byte order, writes to addr.sin_addr. Why binary? The kernel works with numbers, not strings. IP "127.0.0.1" → 0x7F000001. 1 = success
		throw std::runtime_error("inet_pton failed for host");

	// cast to (struct sockaddr *) because bind takes a "generic" sockaddr*, while we have a specific sockaddr_in*
	if (::bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)//Binds the socket to the address/port
		throw std::runtime_error("bind failed");

/*Transitions the socket from "just created" to passive listener state.
128 — backlog size: maximum length of the incoming connection queue the kernel accumulates
before you call accept().
If the queue overflows — new clients get ECONNREFUSED or packets are silently dropped.
On modern Linux the actual backlog is limited by /proc/sys/net/core/somaxconn (usually 128 or 4096).
Passing a larger value is fine — the kernel caps it.*/
	if (::listen(listenFd, 128) < 0)// turn socket into a listening (passive) socket.
									// backlog 128 - "how many clients the kernel can queue
									// before you accept them via accept()"
		throw std::runtime_error("listen failed");

	return listenFd;

}

void	Server::setupListenSockets()
{
	if (cfg_.servers.empty())
		throw std::runtime_error("config has no servers");

	for (size_t si = 0; si < cfg_.servers.size(); ++si)
	{
		const ServerConfig &srv = cfg_.servers[si];

		if (srv.listens.empty())
			throw std::runtime_error("server has no listen directives");

		for (size_t li = 0; li < srv.listens.size(); ++li)
		{
			const ListenConfig &ln = srv.listens[li];

			int	fd = -1;
			try
			{
				fd = createListenSocket(ln.host, ln.port);
				listenFds_.push_back(fd);
				listenFdToServerIndex_[fd] = si;
				LOG_INFO("Listening on %s:%d (fd=%d)", ln.host.c_str(), ln.port, fd);
			}
			catch (...)
			{
				if (fd >= 0)
					::close(fd);
				throw;
			}
		}
	}
}


void	Server::buildPollFds()
{
	pollFds_.clear();
	fdEntries_.clear();

	// 1) listen fds
	for (size_t i = 0; i < listenFds_.size(); ++i)
	{
		struct pollfd	p;
		std::memset(&p, 0, sizeof(p));
		p.fd = listenFds_[i];
		p.events = POLLIN;// For listen socket we wait for only one thing: new connections - POLLIN
		pollFds_.push_back(p);

		FdEntry	e;
		e.fd = p.fd;
		e.kind = FD_LISTEN;
		e.ownerClientFd = -1;
		e.ownerServerIndex = listenFdToServerIndex_[p.fd];
		fdEntries_.push_back(e);
	}

	// 2) client fds + cgi fds
	for (std::map<int, Connection>::iterator it = connections_.begin(); it != connections_.end(); ++it)
	{
		const int	clientFd = it->first;
		Connection	&c = it->second;

		// 2.1) client socket itself
		{
			struct pollfd	p;
			std::memset(&p, 0, sizeof(p));
			p.fd = clientFd;
			p.events = c.wantedPollEvents();
			pollFds_.push_back(p);

			FdEntry			e;
			e.fd = clientFd;
			e.kind = FD_CLIENT;
			e.ownerClientFd = clientFd;
			e.ownerServerIndex = 0;
			fdEntries_.push_back(e);
		}

		// 2.2) CGI stdin/stdout as separate fd entries (if active)
		if (c.hasCgi())
		{
			const int	inFd = c.getCgiStdinFd();
			if (inFd >= 0)
			{
				short	ev = c.wantedCgiStdinEvents();
				if (ev != 0)
				{
					struct pollfd	p;
					std::memset(&p, 0, sizeof(p));
					p.fd = inFd;
					p.events = ev;
					pollFds_.push_back(p);

					FdEntry			e;
					e.fd = inFd;
					e.kind = FD_CGI_STDIN;
					e.ownerClientFd = clientFd;
					e.ownerServerIndex = 0;
					fdEntries_.push_back(e);
				}
			}

			const int	outFd = c.getCgiStdoutFd();
			if (outFd >= 0)
			{
				short	ev = c.wantedCgiStdoutEvents();
				if (ev != 0)
				{
					struct pollfd	p;
					std::memset(&p, 0, sizeof(p));
					p.fd = outFd;
					p.events = ev;
					pollFds_.push_back(p);

					FdEntry			e;
					e.fd = outFd;
					e.kind = FD_CGI_STDOUT;
					e.ownerClientFd = clientFd;
					e.ownerServerIndex = 0;
					fdEntries_.push_back(e);
				}
			}
		}
	}
}

void	Server::handleListenEvent(const FdEntry &e, short revents)
{
	if (revents & POLLIN)
		acceptPendingConnections(e.fd);
}

bool	Server::handleClientEvent(const FdEntry &e, short revents)
{
	std::map<int, Connection>::iterator	it = connections_.find(e.ownerClientFd);
	if (it == connections_.end())
		return false;

	Connection	&c = it->second;

		if (revents & (POLLERR | POLLHUP | POLLNVAL))
	{
		closeConnection(e.ownerClientFd);
		return true; // client closed
	}

	if ((revents & POLLIN) && c.getState() == Connection::READING)
	{
		if (!c.onReadable())
		{
			closeConnection(e.ownerClientFd);
			return true;
		}
		return false;
	}
	else if ((revents & POLLOUT) && c.getState() == Connection::WRITING)
	{
		if (!c.onWritable())
		{
			closeConnection(e.ownerClientFd);
			return true;
		}
		return false;
	}

	if (c.getState() == Connection::CLOSING)
	{
		closeConnection(e.ownerClientFd);
		return true;
	}
	return false;
}

bool	Server::handleCgiEvent(const FdEntry &e, short revents)
{
	std::map<int, Connection>::iterator	it = connections_.find(e.ownerClientFd);
	if (it == connections_.end())
		return false;

	Connection	&c = it->second;

	if (!c.onCgiEvent(e.fd, revents))
	{
		closeConnection(e.ownerClientFd);
		return true;
	}

	// If after CGI processing it switched to CLOSING — close it.
	if (c.getState() == Connection::CLOSING)
	{
		closeConnection(e.ownerClientFd);
		return true;
	}
	return false;
}

void	Server::closeConnection(int clientFd)
{
	std::map<int, Connection>::iterator it = connections_.find(clientFd);
	if (it == connections_.end())
		return;

	LOG_INFO("Closing client fd=%d", clientFd);

	it->second.closeAllFdsAndKillCgiIfAny();
	
	::close(clientFd);
	connections_.erase(it); //important to remove, otherwise a zombie remains in the table
}

void	Server::acceptPendingConnections(int listenFd)
{
	std::map<int, std::size_t>::const_iterator	sit = listenFdToServerIndex_.find(listenFd);
	std::size_t	serverIndex = 0;
	if (sit != listenFdToServerIndex_.end())
		serverIndex = sit->second;

	while (true)// accept in a loop because there may be more than one client,
				// so accept all in one poll to avoid clogging the queue
	{
		struct sockaddr_in	clientAddr; //accept can return not just fd but also the client's address (IP/port)
										//We don't use this yet, but the struct is needed by the signature.
		socklen_t			clientAddrLen = sizeof(clientAddr);
		int					clientFd = ::accept(listenFd, (struct sockaddr *)&clientAddr, &clientAddrLen);
		//new clientFd >= 0 — active connection descriptor with the client
		//Each call = one client from the queue
		//on error -1 we should check errno ideally:
		//EAGAIN / EWOULDBLOCK Queue empty				- normal loop exit
		//EINTR Interrupted by signal					- can retry
		//EMFILE / ENFILE Out of file descriptors		- serious error
		//ECONNABORTED Client disconnected before accept() - can continue loop
		if (clientFd < 0)
		{
			// Project rule: don't inspect errno after I/O.
			// Just stop accepting now; poll will wake us later again.
			// This is a rule from the 42 webserv subject. The point: don't distinguish errors by errno — just
			// stop the current operation and trust poll() to figure it out later.
			// So any < 0 = exit, no reason analysis.
			return;
		}
		try
		{
			setNonBlocking(clientFd);
			connections_.insert(std::make_pair(clientFd, Connection(clientFd, &cfg_, serverIndex)));
			LOG_INFO("Accepted client fd=%d (listenFd=%d)", clientFd, listenFd);
		}
		catch (...)
		{
			::close(clientFd);
		}
	}
}

void	Server::run()
{
	while (true)
	{
		buildPollFds();

		if (pollFds_.empty())
			continue;

		// poll() signature: int poll(struct pollfd *fds, nfds_t nfds, int timeout);
		int	eventCount = ::poll(&pollFds_[0], pollFds_.size(), 1000);
		//poll takes a plain C array of pollfd*, but we have a vector,
		//so we pass a pointer to the first element
		//pollFds_.size() - how many descriptors to monitor
		//1000 - timeout in ms = poll can "sleep" for at most 1 second, even if there are no events.
		//Later make this smarter: timeout based on nearest connection deadline.
		
		if (eventCount <= 0)//Later: on < 0 (error) we could log and handle carefully.
			continue;

		bool	clientClosed = false;
		for (size_t i = 0; i < pollFds_.size(); ++i)
		{
			if (pollFds_[i].revents == 0)
				continue;
		
			const FdEntry	&e = fdEntries_[i];
			short			re = pollFds_[i].revents;

			if (e.kind == FD_LISTEN)
				handleListenEvent(e, re);
			else if (e.kind == FD_CLIENT)
				clientClosed = handleClientEvent(e, re);
			else
				clientClosed = handleCgiEvent(e, re);

			// IF CLIENT CLOSED — break the loop!
			// The fdEntries_ array is no longer valid for this iteration.
			if (clientClosed)
				break;
		}
	}
}
