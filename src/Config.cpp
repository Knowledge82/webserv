/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:20:03 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/12 14:13:16 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Config.hpp"

ListenConfig::ListenConfig()
	: host("127.0.0.1")
	, port(8080)
{
}

LocationConfig::LocationConfig()
	: prefix("/")
	, hasRoot(false)
	, root()
	, hasAlias(false)
	, alias()
	, hasIndex(false)
	, index()
	, hasAutoindex(false)
	, autoindex(false)
	, hasClientMaxBodySize(false)
	, clientMaxBodySize(0)
	, allowedMethods()
	, hasAllowedMethods(false)
	, hasUploadDir(false)
	, uploadDir()
	, hasRedirect(false)
	, redirectCode(0)
	, redirectTarget()
	, hasCgi(false)
	, cgiHandlers()
{
}

ServerConfig::ServerConfig()
	: listens()
	, hasRoot(false)
	, root()
	, hasIndex(false)
	, index()
	, hasAutoindex(false)
	, autoindex(false)
	, hasClientMaxBodySize(false)
	, clientMaxBodySize(0)
	, errorPages()
	, locations()
{
}
