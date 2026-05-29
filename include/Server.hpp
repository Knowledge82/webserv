/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 15:12:41 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/29 14:53:39 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef SERVER_HPP
#define SERVER_HPP

#include "Config.hpp"
#include "Connection.hpp"

#include <poll.h>
#include <map>
#include <vector>
#include <cstddef> // size_t

/* это оркестратор файловых дескрипторов и владелец event loop
Он НЕ знает HTTP. Он знает:

какие fd надо слушать (listen sockets),
какие fd надо мониторить у клиентов,
когда вызывать accept(), recv(), send(),
как закрывать соединения. */
class	Server
{
public:
	explicit	Server(const Config &cfg);
	void		run();

private:
	Config						cfg_;
	std::vector<int>			listenFds_;
	std::vector<struct pollfd>	pollFds_;
	std::map<int, Connection>	connections_; // clientFd -> Connection
	std::map<int, std::size_t>	listenFdToServerIndex_;

	void	setupListenSockets(); // create and setup listen socket: socket/bind/listen/non-blocking
	void	buildPollFds(); // rebuild pfds_ from listenFd_ + all conns_

	void	acceptPendingConnections(int listenFd); // accept new clients and add them to conns_
	void	closeConnection(int fd); // close client's fd and delete Connection from map

};

void		setNonBlocking(int fd);
#endif
