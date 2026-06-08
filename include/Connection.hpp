/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 14:02:44 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/08 10:47:14 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "HttpReply.hpp"
#include "HttpRequest.hpp"
#include "Config.hpp"
#include "EffectiveConfig.hpp"

#include <string>
#include <cstddef>
#include <sys/types.h>
#include <ctime>

class	Connection
{
public:
	enum	State
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

	short	wantedPollEvents() const;

	bool	onReadable();
	bool	onWritable();

	bool	hasCgi() const;
	int		getCgiStdinFd() const;
	int		getCgiStdoutFd() const;
	short	wantedCgiStdinEvents() const;
	short	wantedCgiStdoutEvents() const;
	bool	onCgiEvent(int fd, short revents);
	void	closeAllFdsAndKillCgiIfAny();


private:
	int			fd_;
	State		state_;
	HttpRequest	request_;
	std::string	in_;
	std::string	out_;
	const Config	*cfg_;
	std::size_t	serverIndex_;
	//CGI — это часть обработки запроса данного клиента, значит хранить это в Connection логично:
	pid_t		cgiPid_;
	int			cgiStdinFd_;	// write end pipe, parent пишет
	int			cgiStdoutFd_;	// read end pipe, parent читает 
	std::size_t	cgiInOffset_;	// сколько body уже отправили
	std::string	cgiInData_;		// буфер для записи в CGI
	std::string	cgiOut_;		// накопленный stdout CGI
	bool		cgiStdinClosed_;
	bool		cgiStdoutClosed_;
	time_t		cgiDeadline_;	// таймаут на CGI

	bool	prepareReply(const Http::HttpReply &r);
	bool	tryRedirectToSlashLocation(const ServerConfig &srv,
											const LocationConfig *loc,
											const std::string &uri);
	bool	startCgi(const EffectiveConfig&, const LocationConfig*, const HttpRequest&);
	bool	handleDelete(const EffectiveConfig &eff);
    bool	handleUpload(const EffectiveConfig &eff, const LocationConfig *loc);
};

#endif
