/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Filesystem.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 18:40:55 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/21 19:06:28 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef FILESYSTEM_HPP
#define FILESYSTEM_HPP

#include <string>

namespace Fs
{
	enum	PathKind
	{
		PATH_FILE,
		PATH_DIR,
		PATH_MISSING,
		PATH_FORBIDDEN,
		PATH_ERROR
	};
	bool        readFileToString(const std::string &path, std::string &out);
	std::string joinPath(const std::string &a, const std::string &b);

	PathKind    classifyPath(const std::string &path);
	int         pathKindToHttpStatus(PathKind k);
}

#endif
