/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CgiHandler.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/25 16:09:14 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/11 10:03:32 by vdarsuye         ###   ########.fr       */
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
	bool	isCgiRequest(const LocationConfig *loc, const std::string &uri);
	bool	prepareCgiArgs(const EffectiveConfig &eff,
						const LocationConfig *loc,
						const HttpRequest &req,
						std::string &outExePath,
						std::string &outScriptFile,
						std::string &outWorkDir,
						std::vector<std::string> &outEnv,
						int	&outStatus);

	bool	parseCgiOutput(int &outStatus,
                        std::string &outType,
                        std::string &outBody,
                        const std::string &rawStdout);

}

#endif
