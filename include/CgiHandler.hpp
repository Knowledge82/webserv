/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CgiHandler.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/25 16:09:14 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/26 11:58:59 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include <string>
#include "HttpRequest.hpp"
#include "HttpReply.hpp"
#include "Config.hpp"
#include "EffectiveConfig.hpp"

namespace Http
{
	bool		isCgiRequest(const LocationConfig *loc, const std::string &uri);
	HttpReply	buildCgiReply(const EffectiveConfig &eff,
							const LocationConfig *loc,
							const HttpRequest &req);
}

#endif
