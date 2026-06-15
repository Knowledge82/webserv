/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Filesystem.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 18:41:10 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/21 19:06:20 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Filesystem.hpp"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

namespace Fs
{
	/* Currently we read the entire file into memory. For a small index.html it's fine.
	 * Later we'll do streaming/partial reads
	 * (can be done without poll, but better not to keep gigabytes in RAM)
	 * This is blocking file I/O, acceptable for small files.
	 * For large files, stream or at least limit size.*/
	bool	readFileToString(const std::string &path, std::string &out)
	{
		int	fd = ::open(path.c_str(), O_RDONLY);
		if (fd < 0)
			return false;

		out.clear();

		char	buf[4096];
		while (true)
		{
			ssize_t n = ::read(fd, buf, sizeof(buf));
			if (n == 0)
				break;
			if (n < 0)
			{
				::close(fd);
				return false;
			}
			out.append(buf, n);
		}
		::close(fd);

		return true;
	}


	/*We want to build the path:
	root = ./www
	index = index.html
	result: ./www/index.html

	But there are issues: root may already end with / (./www/), b could be empty,
	a could be empty.
	If we just do a + "/" + b, we might get ./www//index.html or /index.html in the wrong place.
	Limitations (to improve later):
	This is "dumb" string concatenation. It does NOT:
	normalize ..
	remove // inside
	check that the final path stays inside root (path traversal protection).
	*/
	std::string	joinPath(const std::string &a, const std::string &b)
	{
		if (a.empty())
			return b;
		if (b.empty())
			return a;
		if (a[a.size() - 1] == '/') // if already ends with '/'
			return a + b;			// don't add '/'
		return a + "/" + b;			// otherwise add '/'
	}

	PathKind	classifyPath(const std::string &path)
	{
		struct stat	st;

		if (::stat(path.c_str(), &st) == 0)
		{
			if (S_ISDIR(st.st_mode))
				return PATH_DIR;
			return PATH_FILE;
		}

		// stat failed: map errno -> category
		if (errno == ENOENT || errno == ENOTDIR)
			return PATH_MISSING;
		if (errno == EACCES)
			return PATH_FORBIDDEN;

		return PATH_ERROR;
	}

	int			pathKindToHttpStatus(PathKind k)
	{
		if (k == PATH_MISSING)
			return 404;
		if (k == PATH_FORBIDDEN)
			return 403;
		return 500;
	}
}
