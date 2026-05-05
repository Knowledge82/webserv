/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:20:43 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/05 13:29:49 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Connection.hpp"
#include "HttpResponse.hpp"
#include "Log.hpp"

#include <poll.h> //POLLIN/POLLOUT
#include <sys/types.h>
#include <sys/socket.h> //recv/send
#include <unistd.h> // тут не нужен особо, типа для close

Connection::Connection() : fd_(-1), state_(READING)
{
}

Connection::Connection(int fd) : fd_(fd), state_(READING)
{
}

int	Connection::getFd() const
{
	return fd_;
}

Connection::State	Connection::getState() const
{
	return state_;
}

short	Connection::wantedPollEvents() const
{
	//“какие события нам нужны от poll”
	short	ev = 0; // пока ничего не хотим. В реальном сервере обычно так не делают, но для MVP пойдёт.
	if (state_ == READING)//при READING ты просишь poll: “разбуди меня, когда будет что читать”
		ev = ev | POLLIN;
	if (state_ == WRITING && !out_.empty())//нас интересует: “можно ли сейчас писать в сокет”
		ev = ev | POLLOUT;//POLLOUT означает: в сокете есть место в буфере отправки, send скорее всего не заблокируется. Но только если out_ реально содержит данные. Если out_ пуст — писать нечего, значит мы не просим POLLOUT
	
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

	LOG_DEBUG("fd=%d recv bytes=%ld", fd_, (long)n);
	in_.append(buf, n);

	const std::size_t	maxHeaderBytes = 16 * 1024;
	const std::size_t	maxBodyBytes = 1 * 1024 * 1024;
	
	HttpRequest::State	st = request_.parse(in_, maxHeaderBytes, maxBodyBytes);
	if (st == HttpRequest::ERROR)
	{
		int	status = request_.getErrorStatus();
		out_ = HttpResponse::buildErrorResponse(status);
		state_ = WRITING;				// переключаем состояние
	}
	else if (st == HttpRequest::COMPLETE)
	{
		out_ = HttpResponse::buildHelloResponse();// пока так
		state_ = WRITING;				// переключаем состояние
	}
	return true;
}


/*
 *Когда out_ становится пустым после send — ты возвращаешь false, и Server::run() вызывает closeConnection(fd). Это правильно только потому что у тебя в HTTP-ответе Connection: close. Для MVP — нормально. Но когда будешь делать keep-alive — здесь нужно будет переходить обратно в READING, а не закрывать.
 */
bool Connection::onWritable()
{
	if (state_ != WRITING)
		return true;
	if (out_.empty())
		return false;

	ssize_t	n = ::send(fd_, out_.c_str(), out_.size(), 0);
	if (n <= 0)
		return false;

	LOG_DEBUG("fd=%d send bytes=%ld", fd_, (long)n);
	out_.erase(0, n);

	if (out_.empty())
		return false;

	return true;
}
