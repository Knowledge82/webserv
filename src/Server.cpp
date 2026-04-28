/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 15:18:22 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/04/28 17:18:03 by vdarsuye         ###   ########.fr       */
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
	if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
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

void	Server::rebuildPollFds()
{
	pfds_.clear();

	struct pollfd	p;
	std::memset(&p, 0, sizeof(p));
	p.fd = listenFd_;
	p.events = POLLIN;
	pfds_.push_back(p);

	for (std::map<int, Connection>::iterator it = conns_.begin(); it != conns_.end(); ++it)
	{
		struct pollfd	c;
		std::memset(&c, 0, sizeof(c));
		c.fd = it->first;
		c.events = it->second.wantedPollEvents();
		pfds_.push_back(c);
	}
}

void	Server::closeConn(int fd)
{
	std::map<int, Connection>::iterator it = conns_.find(fd);
	if (it != conns_.end())
	{
		LOG_INFO("Closing fd=%d", fd);
		::close(fd);
		conns_.erase(it);
	}
}

void	Server::acceptClients()
{
	while (true)
	{
		struct sockaddr_in	cli;
		socklen_t			len = sizeof(cli);
		int					cfg = ::accept(listenFd_, (struct sockaddr *)&cli, &len);
		if (cfg < 0)
		{
			// Project rule: don't inspect errno after I/O.
			// Just stop accepting now; poll will wake us later again.
			return;
		}
		try
		{
			setNonBlocking(cfg);
			conns_.insert(std::make_pair(cfg, Connection(cfg)));
			LOG_INFO("Accepted client fd=%d", cfg);
		}
		catch (...)
		{
			::close(cfg);
		}
	}
}

void	Server::run()
{
	while (true)
	{
		rebuildPollFds();

		int	ret = ::poll(&pfds_[0], pfds_.size(), 1000);
		if (ret <= 0)
			continue;

		//index 0 is listen fd
		if (pfds_[0].revents & POLLIN)
			acceptClients();

		for (size_t i = 1; i < pfds_.size(); ++i)
		{
			int		fd = pfds_[i].fd;
			short	re = pfds_[i].revents;

			std::map<int, Connection>::iterator	it = conns_.find(fd);
			if (it == conns_.end())
				continue;

			if (re & (POLLERR | POLLHUP | POLLNVAL))
			{
				closeConn(fd);
				continue;
			}

			Connection	&c = it->second;

			if ((re & POLLIN) && c.state() == Connection::READING)
			{
				if (!c.onReadable())
				{
					closeConn(fd);
					continue;
				}
			}
			if ((re & POLLOUT) && c.state() == Connection::WRITING)
			{
				if (!c.onWritable())
				{
					closeConn(fd);
					continue;
				}
			}

			if (c.state() == Connection::CLOSING)
				closeConn(fd);
		}
	}
}
