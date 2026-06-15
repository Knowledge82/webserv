/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Mime.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/22 12:51:24 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/22 12:54:43 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Mime.hpp"

namespace Http
{
	std::string	guessContentType(const std::string &path)
	{
		std::string::size_type	dot = path.find_last_of('.'); //get part after last dot
		if (dot == std::string::npos)
			return "application/octet-stream";

		std::string	ext = path.substr(dot + 1);
		for (std::string::size_type i = 0; i < ext.size(); ++i)
		{
			if (ext[i] >= 'A' && ext[i] <= 'Z')				// convert to lowercase
				ext[i] = static_cast<char>(ext[i] - 'A' + 'a');
		}
															// match against table
		if (ext == "html" || ext == "htm")
			return "text/html";
		if (ext == "css")
			return "text/css";
		if (ext == "js")
			return "application/javascript";
		if (ext == "txt")
			return "text/plain";
		if (ext == "png")
			return "image/png";
		if (ext == "jpg" || ext == "jpeg")
			return "image/jpeg";
		if (ext == "gif")
			return "image/gif";
		if (ext == "ico")
			return "image/x-icon";
		return "application/octet-stream";
	}
}
