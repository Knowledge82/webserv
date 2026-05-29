/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 14:02:44 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/29 17:02:26 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "HttpReply.hpp"
#include "HttpRequest.hpp"
#include "Config.hpp"
#include <string>
#include <cstddef>

class	Connection
{
public:
	enum	State//метка состояния: "что мы сейчас ожидаем от этого fd"
	{
		READING,
		CGI,		// ждём завершения CGI I/O
		WRITING,
		CLOSING
	};

	Connection();
	explicit Connection(int fd, const Config *cfg, std::size_t serverIndex);

	int		getFd() const;
	State	getState() const;

	short	wantedPollEvents() const; // какие события poll должен отслеживать для этого соединения

	bool	onReadable();
	bool	onWritable();

private:
	int			fd_;//это clientFd, который вернул accept
	State		state_;//на каком этапе протокола находится соединение
	HttpRequest	request_;// Connection не должна гадать “сколько ещё читать?”. Она просто спрашивает у парсера состояние.
	std::string	in_;//накопленные входящие байты, прочитанные из сокета. Читаем кусками и склеиваем
	std::string	out_;//исходящий буфер ответа, который ещё не отправлен (или отправлен частично). Потому что send() не гарантирует “отправил всё”. Он может отправить только часть. Поэтому ты хранишь остаток в out_ и дожимаешь позже по POLLOUT.
	const Config	*cfg_; // доступ к конфигу (пока так)
	std::size_t	serverIndex_; // говорит, какой server-block применять (multi-server)
	//CGI — это часть обработки запроса данного клиента, значит хранить это в Connection логично:
	pid_t		cgiPid_;
	int			cgiStdinFd_;	// write end pipe, parent пишет
	int			cgiStdoutFd_;	// read end pipe, parent пишет
	std::size_t	cgiInOffset_;	// сколько body уже отправили
	std::string	cgiOut_;		// накопленный stdout CGI
	bool		cgiStdinClosed_;
	bool		cgiStdoutClosed_;
	time_t		cgiDeadline_;	// таймаут на CGI

	bool	prepareReply(const Http::HttpReply &r);
	bool	tryRedirectToSlashLocation(const ServerConfig &srv,
											const LocationConfig *loc,
											const std::string &uri);
};

#endif
