/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 15:12:41 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/04/30 17:22:19 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef SERVER_HPP
#define SERVER_HPP

#include "Config.hpp"
#include "Connection.hpp"

#include <poll.h>
#include <map>
#include <vector>

class	Server
{
public:
	explicit	Server(const Config& cfg);
	void		run();

private:
	Config						cfg_;
	int							listenFd_;
	std::vector<struct pollfd>	pollFds_; // struct pollfd - C-struct from poll.h
	std::map<int, Connection>	connections_;

	void	setupListenSocket(); // create and setup listen socket: socket/bind/listen/non-blocking
	void	buildPollFds(); // rebuild pfds_ from listenFd_ + all conns_

	void	acceptPendingConnections(); // accept new clients and add them to conns_
	void	closeConnection(int fd); // close client's fd and delete Connection from map

};

#endif
