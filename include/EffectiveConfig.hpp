/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   EffectiveConfig.hpp                                :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 18:46:21 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/21 18:47:30 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef EFFECTIVECONFIG_HPP
#define EFFECTIVECONFIG_HPP

#include <string>
#include <vector>
#include <cstddef>

struct EffectiveConfig
{
	bool						hasRoot;
	std::string					root;

	bool						hasAlias;
	std::string					alias;

	bool						hasIndex;
	std::string					index;

	bool						hasClientMaxBodySize;
	std::size_t					clientMaxBodySize;

	bool						hasAutoindex;
	bool						autoindex;

	bool						hasAllowedMethods;
	std::vector<std::string>	allowedMethods;

	bool						hasUploadDir;
	std::string					uploadDir;

	bool						hasRedirect;
	int							redirectCode;
	std::string					redirectTarget;

	EffectiveConfig();
};

#endif
