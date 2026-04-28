/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:20:43 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/04/28 16:38:02 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Connection.hpp"
#include "Http.hpp"
#include "Log.hpp"

#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

Connection::Connection() : fd_(-1), state_(READING)
{
}

Connection::Connection(int fd) : fd_(fd), state_(READING)
{
}

int	Connection::fd() const
{
	return fd_;
}

Connection::State	Connection::state() const
{
	return state_;
}

short	Connection::wantedPollEvents() const
{
	short	ev = 0;
	if (state_ == READING)
		ev = ev | POLLIN;
	if (state_ == WRITING && !out_.empty())
		ev = ev | POLLOUT;
	
	return ev;
}

bool	Connection::onReadable()
{
	char	buf[4096];
	ssize_t	n = ::recv(fd_, buf, sizeof(buf), 0);
	if (n == 0)
		return false;
	if (n < 0)
		return false;

	LOG_DEBUG("fd=%d recv n=%ld", fd_, (long)n);
	in_.append(buf, n);

	if (state_ == READING && Http::hasEndOfHeaders(in_))
	{
		out_ = Http::buildHelloResponse();
		state_ = WRITING;
	}
	return true;
}

bool Connection::onWritable()
{
	if (state_ != WRITING)
		return true;
	if (out_.empty())
		return false;

	ssize_t	n = ::send(fd_, out_.c_str(), out_.size(), 0);
	if (n <= 0)
		return false;

	LOG_DEBUG("fd=%d send n=%ld", fd_, (long)n);
	out_.erase(0, n);

	if (out_.empty())
		return false;

	return true;
}
