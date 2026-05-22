/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   FilesystemHandler.hpp                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/22 12:40:48 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/22 12:41:25 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef FILESYSTEMHANDLER_HPP
#define FILESYSTEMHANDLER_HPP

#include <string>
#include "HttpReply.hpp"
#include "EffectiveConfig.hpp"
#include "Config.hpp" // for LocationConfig

namespace Http
{
	HttpReply buildFileSystemReply(const EffectiveConfig &eff,
	                               const LocationConfig *loc,
	                               const std::string &uri);
}

#endif
