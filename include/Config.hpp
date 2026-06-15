/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:22:19 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/09 10:17:04 by vdarsuye         ###   ########.fr       */
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


//Location — routing rules for a URL prefix:
//"if URI starts with this prefix, apply these settings".
//A policy chunk: which methods allowed, where to look for files, autoindex on/off,
//upload destination, redirect, CGI.
struct	LocationConfig 
{
	std::string							prefix;

	bool								hasRoot; //hasX + X pattern
	std::string							root;   //Key config idea: inheritance — distinguish "not set" from "set to empty/false":
//If location did NOT set root, inherit from server.root.
//If location set root, override server value.

	bool								hasAlias;
	std::string							alias;

	bool								hasIndex;
	std::string							index;

	bool								hasAutoindex;
	bool								autoindex;
	
	bool								hasClientMaxBodySize;
	std::size_t							clientMaxBodySize;

	std::vector<std::string>			allowedMethods;
	bool								hasAllowedMethods;

	bool								hasUploadDir;
	std::string							uploadDir;

	bool								hasRedirect;
	int									redirectCode;
	std::string							redirectTarget;

	bool								hasCgi;
	std::map<std::string, std::string>	cgiHandlers; // ext -> executable
	
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


