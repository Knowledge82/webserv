/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Path.hpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 18:40:40 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/26 11:26:52 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef PATH_HPP
#define PATH_HPP

#include <string>

namespace Http
{
	bool        endsWithSlash(const std::string &s);

	// URI helpers
	std::string uriPathOnly(const std::string &uri);	// "/a/b?x=1" -> "/a/b"
	std::string uriQueryOnly(const std::string &uri);	// "/a/b?x=1" -> "x=1"
	std::string getExtension(const std::string &uri);	// uses uriPathOnly(), returns ".bla" or ""
	
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
