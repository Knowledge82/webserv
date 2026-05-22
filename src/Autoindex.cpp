/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Autoindex.cpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 18:44:06 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/22 11:55:34 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Autoindex.hpp"
#include "Filesystem.hpp"

#include <dirent.h>

namespace
{
	std::string	htmlEscape(const std::string &s)
	{
		std::string	out;

		out.reserve(s.size());
		for (std::string::size_type i = 0; i < s.size(); ++i)
		{
			char	c = s[i];
			if (c == '&')
				out += "&amp;";
			else if (c == '<')
				out += "&lt;";
			else if (c == '>')
				out += "&gt;";
			else if (c == '"')
				out += "&quot;";
			else 
				out += c;
		}

		return out;
	}
}

namespace Http
{
	bool	appendDirectoryListingHtml(std::string &outHtml,
			const std::string &uriWithSlash,
			const std::string &fsDirPath)
	{
		DIR	*dir = ::opendir(fsDirPath.c_str());
		if (!dir)
			return false;

		outHtml.clear();
		outHtml += "<html><head><meta charset=\"utf-8\"><title>Index of ";
		outHtml += htmlEscape(uriWithSlash);
		outHtml += "</title></head><body>";
		outHtml += "<h1>Index of ";
		outHtml += htmlEscape(uriWithSlash);
		outHtml += "</h1><hr><ul>";

		struct dirent	*ent;
		while ((ent = ::readdir(dir)) != NULL)
		{
			const char	*name = ent->d_name;
			if (!name)
				continue;
			//skip "." and ".."
			if (name[0] == '.' && name[1] == '\0')
				continue;
			if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
				continue;

			std::string	entryName(name);
			std::string	entryFsPath = Fs::joinPath(fsDirPath, entryName);

			//Fs::classifyPath на каждый entry,т.е. stat() на каждый файл, может быть дорого.Пока пофиг.
			bool		isDir = (Fs::classifyPath(entryFsPath) == Fs::PATH_DIR);

			std::string	href = uriWithSlash;
			href += entryName;
			if (isDir)
				href += "/";
		
			outHtml += "<li><a href=\"";
			outHtml += htmlEscape(href);
			outHtml += "\">";
			outHtml += htmlEscape(entryName);
			if (isDir)
				outHtml += "/";
			outHtml += "</a></li>";
		}
		::closedir(dir);

		outHtml += "</ul><hr></body></html>";
		return true;
	}
}
