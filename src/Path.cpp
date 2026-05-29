/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Path.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/22 11:07:50 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/26 11:47:33 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Path.hpp"
#include "Filesystem.hpp"

#include <vector>

namespace
{
	bool	 isHexDigit(char c)
	{
		if (c >= '0' && c <= '9')
			return true;
		if (c >= 'a' && c <= 'f')
			return true;
		if (c >= 'A' && c <= 'F')
			return true;
		return false;
	}

	int		hexValue(char c) // Конвертирует один hex-символ в его числовое значение.
	{
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return 10 + (c - 'a');
		if (c >= 'A' && c <= 'F')
			return 10 + (c - 'A');
		return 0;

		
	}

	// Decode %XX. Returns false on invalid encoding
	// Also enforces policy: decoded '/' is forbidden (prevents encoded slashes).
	bool	urlDecodePath(const std::string &in, std::string &out)
	{
		out.clear();
		out.reserve(in.size());//декодированная строка всегда короче или равна исходной

		for (std::string::size_type i = 0; i < in.size(); ++i)
		{
			// обычный символ - просто копируем и идём дальше.
			char	c = in[i];
			if (c != '%')
			{
				out += c;
				continue;
			}

			if (i + 2 >= in.size())// Need 2 hex digits after '%'. Иначе невалидный URI -> false -> 400
				return false;
			if (!isHexDigit(in[i + 1]) || !isHexDigit(in[i + 2])) // %GZ - невалидно.
				return false;
		
		/*	Пример %2F:
  			hexValue('2') = 2
  			hexValue('F') = 15
  			v = 2 * 16 + 15 = 32 + 15 = 47 = '/' в ASCII */
			int		v = hexValue(in[i + 1]) * 16 + hexValue(in[i + 2]);
			char	decodedChar = static_cast<char>(v);
			
			// forbid encoded slash
			if (decodedChar == '/') // запрещаем %2F ('/')
				return false;		// %2F и %2f дадут 400
			out += decodedChar;
			
			i += 2; //перепрыгиваем два уже обработанных символа. 
					//Цикл сам сделает ++i, итого сдвиг на 3 символа (%, 2, F).
		}
		return true;
	}

	std::string::size_type	findQueryPos(const std::string &uri)
	{
		return uri.find('?');
	}
}

namespace Http
{

	bool					endsWithSlash(const std::string &s)
	{
		if (s.empty())
			return false;
		return (s[s.size() - 1] == '/');
	}

	std::string				uriPathOnly(const std::string &uri)
	{
		std::string::size_type	q = findQueryPos(uri);
		if (q == std::string::npos)
			return uri;
		return uri.substr(0, q);
	}

	std::string				uriQueryOnly(const std::string &uri)
	{
		std::string::size_type	q = findQueryPos(uri);
		if (q == std::string::npos)
			return "";
		return uri.substr(q + 1);
	}

	std::string				getExtension(const std::string &uri)
	{
		std::string				path = uriPathOnly(uri);

		// take last segment only
		std::string::size_type	slash = path.find_last_of('/');
		std::string				name = (slash == std::string::npos) ? path : path.substr(slash + 1);

		// ".bashrc" -> treat as extension
		std::string::size_type	dot = name.find_last_of('.');
		if (dot == std::string::npos)
			return "";
		return name.substr(dot); // includes '.'
	}

	// safeJoin: returns false on error and sets outStatus (400/403)
	bool					safeJoin(const std::string &root, const std::string &rawUri,
					std::string &outFsPath, int &outStatus)
	{
		outStatus = 500;
		outFsPath.clear();

		// Strict: fragment should never be sent in HTTP request line, forbid it
		if (rawUri.find('#') != std::string::npos)
		{
			outStatus = 400;
			return false;
		}

		// fase 1: Отрезает query string и декодирует
		std::string	uriNoQuery = uriPathOnly(rawUri);

		std::string	decoded;
		if (!urlDecodePath(uriNoQuery, decoded))
		{
			outStatus = 400;
			return false;
		}

		// fase 2: Проверяет что URI начинается с /
		if (decoded.empty() || decoded[0] != '/')
		{
			outStatus = 400;
			return false;
		}

		// fase 3: split by '/', normalie '.' and '..'
		std::vector<std::string>	segments;
		std::string					current;

		//Цикл разбивает decoded на сегменты по / и обрабатывает каждый
		for (std::string::size_type i = 0; i <= decoded.size(); ++i)
		{
			char	c = (i < decoded.size()) ? decoded[i] : '/';
			if (c != '/') // Трюк: когда i == decoded.size() — подставляем виртуальный / 
						  // чтобы обработать последний сегмент без дублирования кода.
			{
				current += c;
				continue;
			}

			// finalize segment
			if (current.empty() || current == ".")
			{
				current.clear();
				continue;
			}
			// Если .. пытается выйти за пределы root — segments уже пуст, 
			// некуда pop_back() — это path traversal атака → 403.
			if (current == "..")
			{
				if (segments.empty())
				{
					outStatus = 403;
					return false;
				}
				segments.pop_back();
				current.clear();
				continue;
			}

			segments.push_back(current);
			current.clear();
		}

		// Собирает итоговый путь
		outFsPath = root;
		for (std::size_t i = 0; i < segments.size(); ++i)
			outFsPath = Fs::joinPath(outFsPath, segments[i]);

		outStatus = 200;
		return true;
		/* Полный пример
		root   = "/var/www"
		rawUri = "/files/%2E%2E/secret?token=abc"

		1. uriPathOnly  → "/files/%2E%2E/secret"
		2. urlDecode   → "/files/../secret"
		3. сегменты:
  		 "files" → push → ["files"]
  		 ".."    → pop  → []  → segments.empty() → 403!
		Атака через encoded .. заблокирована. Красиво, блять */
	}
	
	//проверяет “URI начинается с префикса location” + граница, чтобы /img не съедал /images
    bool					startsWithPrefix(const std::string &uri, const std::string &prefix)
    {
        if (prefix.empty())
            return false;
        if (uri.size() < prefix.size())
            return false;
        if (uri.compare(0, prefix.size(), prefix) != 0)
            return false;
        // prefix "/img" should NOT match "/images"
        // accept if:
        // - prefix ends with '/', or
        // - uri is exactly prefix, or
        // - next char is '/'
        if (prefix[prefix.size() - 1] == '/')
            return true;
        if (uri.size() == prefix.size())
            return true;
        if (uri[prefix.size()] == '/')
            return true;

        return false;
    }

	bool					safeJoinAlias(const std::string &aliasBase,
										const std::string &locPrefix,
										const std::string &rawUri,
										std::string &outFsPath,
										int	&outStatus)
	{
		outStatus = 500;
		outFsPath.clear();

		// Alias makes sense only if prefix is non-empty and rawUri starts with it
		if (locPrefix.empty())
		{
			outStatus = 500;
			return false;
		}

		// Must start with prefix (same rule as selectLocation)
		if (!startsWithPrefix(rawUri, locPrefix))
		{
			outStatus = 500;
			return false;
		}

		// Cut prefix from URI: "/directory/nop/a" with prefix "/directory/" -> "nop/a"
		std::string	tail = rawUri.substr(locPrefix.size());

		// Tail must be threated as a path *inside* alias base.
		// safeJoin expects URI-like string starting with '/', so add it.
		std::string	rebasedUri = "/" + tail;

		return safeJoin(aliasBase, rebasedUri, outFsPath, outStatus);
	}

}
