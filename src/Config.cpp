/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:20:03 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/20 17:57:56 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Config.hpp"

ListenConfig::ListenConfig()
	: host("0.0.0.0")
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
