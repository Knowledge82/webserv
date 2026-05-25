/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CgiHandler.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/25 16:09:14 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/25 16:59:37 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include <string>
#include <map>
#include "HttpReply.hpp"
#include "HttpRequest.hpp"
#include "EffectiveConfig.hpp"
#include "Config.hpp"

namespace Http
{
	bool		endsWith(const std::string &s, const std::string &suffix);

	bool		isCgiRequest(const LocationConfig *loc, const std::string &uri);
	bool		findCgiExecutable(std::string &outExe,
								const LocationConfig *loc,
								const std::string &uri);

	HttpReply	buildCgiReply(const EffectiveConfig &eff,
							const LocationConfig *loc,
							const HttpRequest &req);
}

#endif
