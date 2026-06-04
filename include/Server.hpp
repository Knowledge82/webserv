/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 15:12:41 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/04 10:48:05 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef SERVER_HPP
#define SERVER_HPP

#include "Config.hpp"
#include "Connection.hpp"
#include "FdUtils.hpp"

#include <poll.h>
#include <map>
#include <vector>
#include <cstddef> // size_t

class	Server
{
public:
	explicit	Server(const Config &cfg);
	void		run();
//	static int					activeCgiCount;

private:
	enum	FdKind
	{
		FD_LISTEN,
		FD_CLIENT,
		FD_CGI_STDIN,
		FD_CGI_STDOUT
	};

	struct	FdEntry
	{
		int			fd;
		FdKind		kind;
		int			ownerClientFd;		// only for CLIENT/CGI_ (for CLIENT == fd)
		std::size_t	ownerServerIndex;	// only for LISTEN

		FdEntry()
			: fd(-1)
			, kind(FD_CLIENT)
			, ownerClientFd(-1)
			, ownerServerIndex(0)
		{
		}
	};
	Config						cfg_;
	std::vector<int>			listenFds_;
	
	std::map<int, Connection>	connections_; // clientFd -> Connection
	std::map<int, std::size_t>	listenFdToServerIndex_;

	// Ключ: pollFds_[i] и fdEntries_[i] всегда синхронны по индексу.
	std::vector<struct pollfd>	pollFds_;
	std::vector<FdEntry>		fdEntries_; // same index as pollFds_;

	void	setupListenSockets(); // create and setup listen socket: socket/bind/listen/non-blocking
	void	buildPollFds(); // rebuild pfds_ from listenFd_ + all conns_

	void	acceptPendingConnections(int listenFd); // accept new clients and add them to conns_
	void	closeConnection(int fd); // close client's fd and delete Connection from map

	// dispatch helpers
	void	handleListenEvent(const FdEntry &e, short revents);
	bool	handleClientEvent(const FdEntry &e, short revents);
	bool	handleCgiEvent(const FdEntry &e, short revents);

};

#endif
