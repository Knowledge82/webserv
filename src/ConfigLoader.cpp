/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigLoader.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/06 11:47:33 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/06 11:57:00 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ConfigLoader.hpp"
#include "ConfigParser.hpp"

Config	ConfigLoader::loadFromFile(const std::string &path)
{
	ConfigParser p(path);
	return p.parseConfig();
}

Config	ConfigLoader::loadDefault()
{
	Config cfg;
	ServerConfig srv;
	ListenConfig l;
	srv.listens.push_back(l);
	cfg.servers.push_back(srv);
	return cfg;
}

