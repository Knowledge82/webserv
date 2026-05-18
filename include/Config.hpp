/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:22:19 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/18 14:09:16 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <cstddef> //size_t

struct	ListenConfig
{
	std::string	host;
	int			port;

	ListenConfig();
};


//Location — Это “правила для URL-префикса”, 
//правило маршрутизации:“если URI начинается с этого префикса, применяй вот эти настройки”.
//это кусок политики: какие методы разрешены, откуда брать файлы, нужен ли автоиндекс, куда сохранять upload, делать ли редирект, запускать ли CGI.
struct	LocationConfig 
{
	std::string					prefix;

	bool						hasRoot; //Пара hasX + X
	std::string					root;   //Это ключевая идея конфигов: наследование и отличие “не задано” от “задано пустое/false”:
//Если location НЕ задавал root, то root должен наследоваться от server.root.
//Если location задал root, он перекрывает серверный.

	bool						hasIndex;
	std::string					index;

	bool						hasAutoindex;
	bool						autoindex;

	std::vector<std::string>	allowedMethods;
	bool						hasAllowedMethods;

	bool						hasUploadDir;
	std::string					uploadDir;

	bool						hasRedirect;
	int							redirectCode;
	std::string					redirectTarget;

	LocationConfig();
};

struct	ServerConfig
{
	std::vector<ListenConfig>	listens;

	bool						hasRoot;
	std::string					root;

	bool						hasIndex;
	std::string					index;
	
	bool						hasAutoindex;
	bool						autoindex;

	bool						hasClientMaxBodySize;
	std::size_t					clientMaxBodySize;

	std::map<int, std::string>	errorPages;

	std::vector<LocationConfig>	locations;

	ServerConfig();
};

struct	Config
{
	std::vector<ServerConfig>	servers;
};

#endif


