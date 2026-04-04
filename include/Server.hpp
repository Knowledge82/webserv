/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 15:12:41 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/04/04 15:18:18 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef SERVER_HPP
#define SERVER_HPP

#include "Config.hpp"
#include "Connection.hpp"

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
	std::vector<struct pollfd>	pfds_;
	std::map<int, Connection>	conns_;

	void	setupListenSoket();
	void	rebuildPollFds();

	void	acceptClients();
	void	closeConn(int fd);

};

#endif
