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
	/* Сейчас мы читаем файл целиком в память. Для небольшого index.html норм.
	 * Потом сделаем streaming/чтение частями
	 * (и это можно делать без poll, но лучше не держать гигабайты в RAM)
	 * это блокирующее чтение файла, но для маленьких файлов нормально.
		позже для больших файлов лучше потоково отправлять (или хотя бы лимитировать размер).*/
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


	/*Мы хотим получить путь:
	root = ./www
	index = index.html
	итог: ./www/index.html

	Но есть проблемы: root может уже заканчиваться на / (./www/), b может быть пустой,
	a может быть пустой.
	Если просто делать a + "/" + b, можно получить ./www//index.html или /index.html не там, где надо
	Ограничения (которые мы потом улучшим)
	Это “тупое” склеивание строк. Оно не: 
	нормализует ..
	не убирает // внутри
	не проверяет, что итоговый путь остаётся внутри root (защита от path traversal).
	*/
	std::string	joinPath(const std::string &a, const std::string &b)
	{
		if (a.empty())
			return b;
		if (b.empty())
			return a;
		if (a[a.size() - 1] == '/') // если уже заканчивается на '/'
			return a + b;			// то не добавлять '/'
		return a + "/" + b;			// иначе добавить
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
