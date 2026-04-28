/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:20:03 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/04/28 16:19:40 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

Config::Config() : host("0.0.0.0"), port(8080)
{
}

static void	parseListen(Config &cfg, const std::string &value)
{
	// expected host:port
	std::string::size_type	pos = value.find(':');
	if (pos == std::string::npos)
		throw std::runtime_error("listen must be host:port");

	cfg.host = value.substr(0, pos);

	std::string	portStr = value.substr(pos + 1);
	if (portStr.empty())
		throw std::runtime_error("port is empty");

	std::istringstream	iss(portStr);
	int	p = -1;
	iss >> p;
	if (!iss || p < 1 || p > 65535)
		throw std::runtime_error("invalid port");

	cfg.port = p;
}

Config	ConfigLoader::loadFromFile(const std::string &path)
{
	std::ifstream	in(path.c_str());
	if (!in.is_open())
		throw std::runtime_error("cannot open config file");

	Config		cfg;

	std::string	line;
	while (std::getline(in, line))
	{
		//remove comments starting with #
		std::string::size_type	hash = line.find('#');
		if (hash != std::string::npos)
			line = line.substr(0, hash);

		std::istringstream	iss(line);
		std::string			key;
		iss >> key;
		if (!iss || key.empty())
			continue;

		if (key == "listen")
		{
			std::string	value;
			iss >> value;
			if (!iss)
				throw std::runtime_error("listen requires a value");
			parseListen(cfg, value);
		}
		else
		{
			throw std::runtime_error("unknown directive: " + key);
		}
	}

	return cfg;
}

Config	ConfigLoader::loadDefault()
{
	return Config();
}
