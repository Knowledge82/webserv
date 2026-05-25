/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CgiHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/25 18:27:37 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/25 18:30:47 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "CgiHandler.hpp"
#include "Filesystem.hpp"
#include "Path.hpp"

#include <unistd.h> // fork, pipe, dup2, execve, chdir, close
#include <sys/wait.h> // waitpid
#include <cstdlib> // getenv
#include <cerrno>
#include <sstream>
#include <vector>


namespace
{
	
}

	bool		endsWith(const std::string &s, const std::string &suffix);

	bool		isCgiRequest(const LocationConfig *loc, const std::string &uri);
	bool		findCgiExecutable(std::string &outExe,
								const LocationConfig *loc,
								const std::string &uri);

	HttpReply	buildCgiReply(const EffectiveConfig &eff,
							const LocationConfig *loc,
							const HttpRequest &req);
}
