/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:20:43 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/12 18:15:41 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Connection.hpp"
#include "HttpResponse.hpp"
#include "Log.hpp"

#include <poll.h> //POLLIN/POLLOUT
#include <sys/types.h>
#include <sys/socket.h> //recv/send
#include <unistd.h> // тут не нужен особо, типа для close
#include <fcntl.h>

/* Сейчас мы читаем файл целиком в память. Для небольшого index.html норм. Потом сделаем streaming/чтение частями (и это можно делать без poll, но лучше не держать гигабайты в RAM) */

static bool	readFileToString(const std::string &path, std::string &out)
{
	int	fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return false;

	out.clear();

	char	buf[4096];
	while (true)
	{
		ssize_t n = ::read(fd, buf, sizeof(buf));
		if (n == 0)
			break;
		if (n < 0)
		{
			::close(fd);
			return false;
		}
		out.append(buf, n);
	}

	::close(fd);

	return true;
}


/*Мы хотим получить путь:
	root = ./www
	index = index.html → итог: ./www/index.html

	Но есть проблемы: root может уже заканчиваться на / (./www/), b может быть пустой,
	a может быть пустой.
	Если просто делать a + "/" + b, можно получить ./www//index.html или /index.html не там, где надо
	Ограничения (которые мы потом улучшим)
	Это “тупое” склеивание строк. Оно не: 
	нормализует ..
	не убирает // внутри
	не проверяет, что итоговый путь остаётся внутри root (защита от path traversal).
	Это будет отдельная, более важная стадия, когда начнём отдавать root + uri.
	*/
static std::string	joinPath(const std::string &a, const std::string &b)
{
	if (a.empty())
		return b;
	if (b.empty())
		return a;
	if (a[a.size() - 1] == '/')
		return a + b;
	return a + "/" + b;
}
/*	Если хочешь “по‑профи”, следующий шаг после того, как оно заработает:
	вынести readFileToString в отдельный модуль типа FileUtils (и покрыть тестами),
	сделать safeJoin(root, uri) с нормализацией и защитой от ... */


Connection::Connection()
	: fd_(-1)
	, state_(READING)
{
}

Connection::Connection(int fd, const Config *cfg)
	: fd_(fd)
	, state_(READING)
	, cfg_(cfg)
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
		if (!cfg_ || cfg_->servers.empty())
		{
			out_ = HttpResponse::buildErrorResponse(500);
			state_ = WRITING;
			return true;
		}

		const ServerConfig &srv = cfg_->servers[0]; // ВРЕМЕННО: первый сервер

		if (request_.getMethod() != "GET")
		{
			out_ = HttpResponse::buildErrorResponse(405);
			state_ = WRITING;
			return true;
		}
		
		if (request_.getUri() != "/")
		{
			out_ = HttpResponse::buildErrorResponse(404);
			state_ = WRITING;
			return true;
		}
		
		if (!srv.hasRoot || !srv.hasIndex)
		{
			out_ = HttpResponse::buildErrorResponse(500);
			state_ = WRITING;
			return true;
		}

		const std::string	path = joinPath(srv.root, srv.index);

		std::string	body;
		if (!readFileToString(path, body))
		{
			out_ = HttpResponse::buildErrorResponse(404);
			state_ = WRITING;
			return true;
		}
		
		out_ = HttpResponse::buildResponse(200, "text/html", body);
		state_ = WRITING;
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
