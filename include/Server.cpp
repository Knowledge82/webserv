/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 15:18:22 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/04/09 18:29:13 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
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

static void	setNonBlocking(int fd)
{
	int	flags = ::fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		throw std::runtime_error("fcntl(F_GETFL) failed");
	if (::fcntl(fd, FSETFL, flags | O_NONBLOCK) < 0)
		throw std::runtime_error("fcntl(F_SETFL) failed");
}

Server::Server(const Config &cfg) : cfg_(cfg), listenFd_(-1)
{
	setupListenSocket();
}

void	Server::setupListenSocket()
{
	listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
	if (listenFd_ < 0)
		throw std::runtime_error("socket failed");

	int	yes = 1;
	::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	setNonBlocking(listenFd_);

	struct sockaddr_in	addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<unsigned short>(cfg_.port));
	if (::inet_pton(AF_INET, cfg_.host.c_str(), &addr.sin_addr) != 1)
		throw std::runtime_error("inet_pton failed for host");

	if (::bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		throw std::runtime_error("bind failed");

	if (::listen(listenFd_, 128) < 0)
		throw std::runtime_error("listen failed");

	LOG_INFO("Listening on %s:%d (fd=%d)", cfg_.host.c_str(), cfg_.port, listenFd_);
}
