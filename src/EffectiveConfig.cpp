/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   EffectiveConfig.cpp                                :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/22 12:16:38 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/09 10:18:57 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "EffectiveConfig.hpp"

EffectiveConfig::EffectiveConfig()
				: hasRoot(false)
				, root()
				, hasAlias(false)
				, alias()
				, hasIndex(false)
				, index()
				, hasClientMaxBodySize(false)
				, clientMaxBodySize(0)
				, hasAutoindex(false)
				, autoindex(false)
				, hasAllowedMethods(false)
				, allowedMethods()
				, hasUploadDir(false)
				, uploadDir()
				, hasRedirect(false)
				, redirectCode(0)
				, redirectTarget()
				, hasCgi(false)
{
}

