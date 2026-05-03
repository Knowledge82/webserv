/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:20:03 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/02 18:10:13 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

Config::Config() : host("0.0.0.0"), port(8080)
{
}

//это приватная функция-утилита для Config.cpp (static)
static void	parseListen(Config &cfg, const std::string &value)
{
	// expected "host:port"
	std::string::size_type	pos = value.find(':'); //size_type = “беззнаковый тип для индексов строки”
	if (pos == std::string::npos)
		throw std::runtime_error("listen must be host:port");

	cfg.host = value.substr(0, pos);// from start str to ":" = host

	std::string	portStr = value.substr(pos + 1); // from ":" + 1 to end = str of port
	if (portStr.empty())
		throw std::runtime_error("port is empty");


	/*
	 *Почему iss, а не atoi:
	atoi не умеет нормально сигналить ошибки (вернёт 0 и ты не отличишь “0” от “ошибка”).
	istringstream даёт возможность проверить !iss — “не распарсилось”.
	Важное ограничение твоего текущего парсинга:
	iss >> p съест число, но строка "8080abc" превратится в 8080 и iss останется “ok”. То есть сейчас это пройдёт. Если захочешь строгий режим — после iss >> p нужно ещё проверить, что остатка нет (например, попытаться прочитать ещё один символ).
	 */
	std::istringstream	iss(portStr); // str of port -> int of port
	int	portInt = -1;
	iss >> portInt;

	char	extra; // strict port validation
	if (!iss || (iss >> extra) || portInt < 1 || portInt > 65535)
		throw std::runtime_error("invalid port");

	cfg.port = portInt;
}

static void	removeComment(std::string &line)
{
	std::string::size_type	hashPos = line.find('#');
	if (hashPos != std::string::npos)
		line.erase(hashPos);
}

Config	ConfigLoader::loadFromFile(const std::string &path)
{
	std::ifstream	configFile(path.c_str());
	if (!configFile.is_open())
		throw std::runtime_error("cannot open config file");

	Config		cfg;

	std::string	line;
	while (std::getline(configFile, line))
	{
		removeComment(line);//remove comments starting with #. Это хороший тон

		std::istringstream	iss(line);// пропускаем пустые строки/строки с пробелами
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
			throw std::runtime_error("unknown directive: " + key);
	}

	return cfg;
}

Config	ConfigLoader::loadDefault()
{
	return Config();// дефолтный конфиг. это правильный паттерн
}

//Если расширять конфиг под webserv:
//несколько server блоков (по портам/именам)
//root, index, error_page, client_max_body_size
//location блоки с матчингом пути
//cgi_pass и mapping расширений
