/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Path.hpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 18:40:40 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/22 11:15:18 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef PATH_HPP
#define PATH_HPP

#include <string>

namespace Http
{
	bool        endsWithSlash(const std::string &s);

	std::string stripQuery(const std::string &uri);

	bool        startsWithPrefix(const std::string &uri, const std::string &prefix);

	bool        safeJoin(const std::string &root,
	                     const std::string &rawUri,
	                     std::string &outFsPath,
	                     int &outStatus);

	bool        safeJoinAlias(const std::string &aliasBase,
	                          const std::string &locPrefix,
	                          const std::string &rawUri,
	                          std::string &outFsPath,
	                          int &outStatus);
}

#endif
