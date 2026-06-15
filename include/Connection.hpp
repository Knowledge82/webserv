/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 14:02:44 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/08 17:27:45 by vdarsuye         ###   ########.fr       */
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
		CGI,		// waiting for CGI I/O to complete
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
	//CGI is part of this client's request processing — storing it in Connection is logical:
	pid_t		cgiPid_;
	int			cgiStdinFd_;	// write end of pipe, parent writes
	int			cgiStdoutFd_;	// read end of pipe, parent reads
	std::size_t	cgiInOffset_;	// how much body we've already sent
	std::string	cgiInData_;		// buffer for writing to CGI
	std::string	cgiOut_;		// accumulated CGI stdout
	bool		cgiStdinClosed_;
	bool		cgiStdoutClosed_;
	time_t		cgiDeadline_;	// CGI timeout
	
	// === STREAMING FILE OUTPUT FIELDS ===
	int         fileStreamFd_;       // Open file descriptor for reading (initially -1)
	std::size_t fileStreamBytesLeft_; // Bytes remaining to send (Content-Length)

	bool	handleStartSendingFile(const std::string &filePath, std::size_t fileSize);
	
	bool	prepareReply(const Http::HttpReply &r);
	bool	tryRedirectToSlashLocation(const ServerConfig &srv,
											const LocationConfig *loc,
											const std::string &uri);
	bool	startCgi(const EffectiveConfig&, const LocationConfig*, const HttpRequest&);
	bool	handleDelete(const EffectiveConfig &eff);
    bool	handleUpload(const EffectiveConfig &eff, const LocationConfig *loc);
};

#endif
