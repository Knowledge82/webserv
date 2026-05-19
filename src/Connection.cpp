/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:20:43 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/19 18:30:47 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Connection.hpp"
#include "HttpResponse.hpp"
#include "Log.hpp"

#include <poll.h> //POLLIN/POLLOUT
#include <sys/types.h>
#include <sys/socket.h> //recv/send
#include <sys/stat.h> // информация о файлах и директориях - "узнать всё о файле не открывая его"
					// int stat(const char *path, struct stat *buf);
					// Заполняет структуру struct stat:
					// struct stat {
					// mode_t  st_mode;   // тип файла и права доступа
					// off_t   st_size;   // размер в байтах
					// time_t  st_mtime;  // время последней модификации
					// + ещё много полей
					// };
					// Возвращает:
					// 0 — успех, структура st заполнена
					// -1 — ошибка (файл не существует, нет прав, битый путь)
#include <unistd.h> // close
#include <fcntl.h>
#include <dirent.h>
#include <vector> // std::vector<std::string> in EffectiveConfig
#include <errno.h>


// ================================== UTILS ==============================
namespace
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
	
	// ------------------------ isDirectory -> classifyPath -----------------------
	/*Почему isDirectory() уже не хватает
	isDirectory(path) возвращает только true/false и теряет причину:
	false потому что это файл → нормально
	false потому что не существует → должен быть 404
	false потому что нет прав → должен быть 403
	false потому что ошибка → 500
	А тебе нужно разное поведение.
	PathKind + classifyPath() как раз сохраняет смысл: что это за path и что пошло не так.*/
	bool		isDirectory(const std::string &path) // DEPRICATED
	{
		struct stat	st;
		if (::stat(path.c_str(), &st) != 0)
			return false;
		return S_ISDIR(st.st_mode);
	}

	enum		PathKind
	{
		PATH_FILE,
		PATH_DIR,
		PATH_MISSING,
		PATH_FORBIDDEN,
		PATH_ERROR
	};

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

	std::string	guessContentType(const std::string &path)
	{
		std::string::size_type	dot = path.find_last_of('.'); //взять часть после последней точки
		if (dot == std::string::npos)
			return "application/octet-stream";

		std::string	ext = path.substr(dot + 1);
		for (std::string::size_type i = 0; i < ext.size(); ++i)
		{
			if (ext[i] >= 'A' && ext[i] <= 'Z')				// привести к lower-case
				ext[i] = static_cast<char>(ext[i] - 'A' + 'a');
		}
															// сопоставить с таблицей
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

	struct	EffectiveConfig //“готовые к применению” настройки для конкретного запроса
	{
		// effective root
		bool						hasRoot;
		std::string					root;

		// effective index
		bool						hasIndex;
		std::string					index;

		// effective autoindex
		bool						hasAutoindex;
		bool						autoindex;

		// effective allowed methods
		bool						hasAllowedMethods;
		std::vector<std::string>	allowedMethods;

		// effective upload dir (not used yet)
		bool						hasUploadDir;
		std::string					uploadDir;

		// effective redirect
		bool						hasRedirect;
		int							redirectCode;
		std::string					redirectTarget;

		EffectiveConfig()
			: hasRoot(false)
			, root()
			, hasIndex(false)
			, index()
			, hasAutoindex(false)
			, autoindex(false)
			, hasAllowedMethods(false)
			, allowedMethods()
			, hasUploadDir(false)
			, uploadDir()
			, hasRedirect(false)
			, redirectCode(0)
			, redirectTarget()
		{
		}
	};

	//проверяет “URI начинается с префикса location” + граница, чтобы /img не съедал /images
	// такое себе название. может checkPrefix или что-то такое?
	bool	startsWithPrefix(const std::string &uri, const std::string &prefix)
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

	//пробегает все locations и выбирает самый длинный матч
	const LocationConfig	*selectLocation(const std::vector<LocationConfig> &locations,
											const std::string &uri)
	{
		const LocationConfig	*best = NULL;
		std::size_t				bestLen = 0;

		for (std::size_t i = 0; i < locations.size(); ++i)
		{
			const LocationConfig	&loc = locations[i];
			const std::string		&prefix = loc.prefix;

			if (!startsWithPrefix(uri, prefix))
				continue;

			if (prefix.size() >= bestLen)
			{
				best = &loc;
				bestLen = prefix.size();
			}
		}
		
		return best;
	}

	//устанавливает server defaults, а потом сверху location overrides
	EffectiveConfig	buildEffectiveConfig(const ServerConfig &srv, const LocationConfig *loc)
	{
		EffectiveConfig	eff;

		// сначала кладём значения server-level, потом location override
		
		// root
		if (srv.hasRoot)
		{
			eff.hasRoot = true;
			eff.root = srv.root;
		}
		if (loc && loc->hasRoot)
		{
			eff.hasRoot = true;
			eff.root = loc->root;
		}

		// index
		if (srv.hasIndex)
		{
			eff.hasIndex = true;
			eff.index = srv.index;
		}
		if (loc && loc->hasIndex)
		{
			eff.hasIndex = true;
			eff.index = loc->index;
		}

		// autoindex
		if (srv.hasAutoindex)
		{
			eff.hasAutoindex = true;
			eff.autoindex = srv.autoindex;
		}

		if (loc && loc->hasAutoindex)
		{
			eff.hasAutoindex = true;
			eff.autoindex = loc->autoindex;
		}

		// allow_methods (location-only in our config)
		if (loc && loc->hasAllowedMethods)
		{
			eff.hasAllowedMethods = true;
			eff.allowedMethods = loc->allowedMethods;
		}

		// upload_dir (location-only)
		if (loc && loc->hasUploadDir)
		{
			eff.hasUploadDir = true;
			eff.uploadDir = loc->uploadDir;
		}

		// return (redirect) (location-only)
		if (loc && loc->hasRedirect)
		{
			eff.hasRedirect = true;
			eff.redirectCode = loc->redirectCode;
			eff.redirectTarget = loc->redirectTarget;
		}

		return eff;
	}

	//если allow_methods задан, проверяет, входит ли метод в список.
	bool	isAllowedMethod(const std::string &method, const EffectiveConfig &eff)
	{
		if (!eff.hasAllowedMethods)
			return true;

		for (std::size_t i = 0; i < eff.allowedMethods.size(); ++i)
		{
			if (eff.allowedMethods[i] == method)
				return true;
		}
		return false;
	}
	
	// generate HTML listing
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
			std::string	entryFsPath = joinPath(fsDirPath, entryName);

			bool		isDir = isDirectory(entryFsPath);

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

	bool	endsWithSlash(const std::string &s)
	{
		if (s.empty())
			return false;
		return (s[s.size() - 1] == '/');
	}

	
	// ------------------- SAFE JOIN (root + uri) ------------------------
	// Policy:
	// // - URI may contain query (?a=b) -> ignored for filesystem mapping
	// - Fragment '#' is forbidden -> 400
	// - Percent-decoding is supported
	// - Encoded slash (%2F/%2f) is forbidden -> 400
	// - Path is normalized (., ..)
	// - Attempt to escape root via ".." -> 403
	
	bool	 isHexDigit(char c) // можно isxdigit() из стандартной библиотеки
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

	std::string	stripQuery(const std::string &uri)
	{
		std::string::size_type	q = uri.find('?');
		if (q == std::string::npos)
			return uri;				//   /files/doc.pdf   →  /files/doc.pdf
		return uri.substr(0, q);	//   /search?q=hello  →  /search
	}

	// safeJoin: returns false on error and sets outStatus (400/403)
	bool	safeJoin(const std::string &root, const std::string &rawUri,
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
		std::string	uriNoQuery = stripQuery(rawUri);

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
			outFsPath = joinPath(outFsPath, segments[i]);

		outStatus = 200;
		return true;
		/* Полный пример
		root   = "/var/www"
		rawUri = "/files/%2E%2E/secret?token=abc"

		1. stripQuery  → "/files/%2E%2E/secret"
		2. urlDecode   → "/files/../secret"
		3. сегменты:
  		 "files" → push → ["files"]
  		 ".."    → pop  → []  → segments.empty() → 403!
		Атака через encoded .. заблокирована. Красиво, блять */
	}

}

// ============================================================================

Connection::Connection() // по факту может и не нужен, но оставить можно
	: fd_(-1)
	, state_(READING)
	, cfg_(NULL)
	, serverIndex_(0)
{
}

Connection::Connection(int fd, const Config *cfg, std::size_t serverIndex) // main constructor
	: fd_(fd)
	, state_(READING)
	, cfg_(cfg) // cfg_ нужен, чтобы достать root/index/max_body_size
	, serverIndex_(serverIndex) //нужен, чтобы выбрать правильный server block
								//(после listenFd→serverIndex mapping)
{
}

int	Connection::getFd() const
{
	return fd_;
}

Connection::State	Connection::getState() const
{
	return state_;
}

short	Connection::wantedPollEvents() const
{
	//“какие события нам нужны от poll”. Connection говорит Server что ему нужно от poll.
	//Очень важная идея: Server не должен знать протокол, он просто выполняет то, что Connection просит.
	short	ev = 0; // пока ничего не хотим. В реальном сервере обычно так не делают, но для MVP пойдёт.
	if (state_ == READING)//при READING ты просишь poll: “разбуди меня, когда будет что читать”
		ev = ev | POLLIN;
	if (state_ == WRITING && !out_.empty())//нас интересует: “можно ли сейчас писать в сокет”
		ev = ev | POLLOUT;//POLLOUT означает: в сокете есть место в буфере отправки, send скорее всего не заблокируется. Но только если out_ реально содержит данные. Если out_ пуст — писать нечего, значит мы не просим POLLOUT
	
	return ev;
}

bool	Connection::onReadable()
{
	// ----------------- layer 1: network read --------------------------

	char	buf[4096];
	ssize_t	n = ::recv(fd_, buf, sizeof(buf), 0);
	if (n == 0) // клиент закрыл соединение
		return false;
	if (n < 0) // ошибка
		return false;

	LOG_DEBUG("fd=%d recv bytes=%ld", fd_, (long)n);
	in_.append(buf, n);

	// ----------------- layer 2: determine limits and parse HTTP --------------------------

	const std::size_t	maxHeaderBytes = 16 * 1024; // 16KB
	std::size_t			maxBodyBytes = 1 * 1024 * 1024; //default for now 1MB or
	if (cfg_ && serverIndex_ < cfg_->servers.size())
	{
		const ServerConfig	&srv = cfg_->servers[serverIndex_];
		if (srv.hasClientMaxBodySize)
			maxBodyBytes = srv.clientMaxBodySize;
	}
	
	// parse модифицирует in_:
	// когда найдены заголовки, он вырезает их из in_. потом вырезает body из in_
	// оставшееся в in_ может быть “лишними байтами” (в будущем — pipelining/следующий запрос)
	HttpRequest::State	st = request_.parse(in_, maxHeaderBytes, maxBodyBytes);
	
	// ----------------- layer 3: react to parser state --------------------------
	
	if (st == HttpRequest::ERROR)
	{
		int	status = request_.getErrorStatus();
		out_ = HttpResponse::buildErrorResponse(status);
		state_ = WRITING;
		return true;
	}

	if (st != HttpRequest::COMPLETE)
		return true;
	
	// ----------------- layer 4: validate config --------------------------

	if (!cfg_ || cfg_->servers.empty())
	{
		out_ = HttpResponse::buildErrorResponse(500);
		state_ = WRITING;
		return true;
	}
	if (serverIndex_ >= cfg_->servers.size())	// serverIndex в диапазоне
	{
		out_ = HttpResponse::buildErrorResponse(500);
		state_ = WRITING;
		return true;
	}

	const ServerConfig		&srv = cfg_->servers[serverIndex_];
	const std::string		uri = request_.getUri();
	
	const LocationConfig	*loc = selectLocation(srv.locations, request_.getUri());
	EffectiveConfig			eff = buildEffectiveConfig(srv, loc);

	// Redirect (return) has priority over everything else
	if (eff.hasRedirect)
	{
		out_ = HttpResponse::buildRedirectResponse(eff.redirectCode, eff.redirectTarget);
		state_ = WRITING;
		return true;
	}

	// Method policy
	if (!isAllowedMethod(request_.getMethod(), eff))
	{
		out_ = HttpResponse::buildErrorResponse(405);
		state_ = WRITING;
		return true;
	}
	if (request_.getMethod() != "GET")			// проверяем метод GET
	{
		out_ = HttpResponse::buildErrorResponse(405);
		state_ = WRITING;
		return true;
	}

	// Must have root
	if (!eff.hasRoot)
	{
		out_ = HttpResponse::buildErrorResponse(500);
		state_ = WRITING;
		return true;
	}
	
	// ----------------- layer 5: map URI -> filesystem path --------------------------
	// заменили заглушку containsDotDot(uri)
	// и опасное строковое склеивание joinPath(eff.root, uri.substr(1))
	// на пиздатую safeJoin, которая:
	// выкинет query, сделает URL-decode, запретит %2F, нормализует ./.., не даст выйти выше root.
	std::string	path;

	// Special-case "/" -> root/index
	if (uri == "/")
	{
		if (!eff.hasIndex)
		{
			out_ = HttpResponse::buildErrorResponse(403);
			state_ = WRITING;
			return true;
		}
		path = joinPath(eff.root, eff.index);

		PathKind	pk = classifyPath(path);
		if (pk != PATH_FILE)
		{
			int	status;
			if (pk == PATH_DIR)
				status = 403;
			else
				status = pathKindToHttpStatus(pk);
			
			out_ = HttpResponse::buildErrorResponse(status);
			state_ = WRITING;
			return true;
		}

		std::string	body;
		if (!readFileToString(path, body))
		{
			out_ = HttpResponse::buildErrorResponse(500);
			state_ = WRITING;
			return true;
		}
		
		out_ = HttpResponse::buildResponse(200, guessContentType(path), body);
		state_ = WRITING;
		return true;
	}
	
	// Non-root URI: safeJoin does decode + nomalize + traversal protection
	{
		int	safeStatus = 200;
		if (!safeJoin(eff.root, uri, path, safeStatus))
		{
			out_ = HttpResponse::buildErrorResponse(safeStatus);
			state_ = WRITING;
			return true;
		}
	}

	PathKind	pk = classifyPath(path);

	// If stat says missing/forbidden/error: answer immediately
	if (pk == PATH_MISSING || pk == PATH_FORBIDDEN || pk == PATH_ERROR)
	{
		out_ = HttpResponse::buildErrorResponse(pathKindToHttpStatus(pk));
		state_ = WRITING;
		return true;
	}

	// Directory flow
	if (pk == PATH_DIR)
	{
		// Redirect "/dir" -> "/dir/" to keep relative links correct
		if (!endsWithSlash(uri))
		{
			out_ = HttpResponse::buildRedirectResponse(301, uri + "/");
			state_ = WRITING;
			return true;
		}

		// Try index first (if configured)
		if (eff.hasIndex)
		{
			std::string	indexPath = joinPath(path, eff.index);
			PathKind	ik = classifyPath(indexPath);

			if (ik == PATH_FILE)
			{
				std::string	body;
				if (!readFileToString(indexPath, body))
				{
					out_ = HttpResponse::buildErrorResponse(500);
					state_ = WRITING;
					return true;
				}
				out_ = HttpResponse::buildResponse(200, guessContentType(indexPath), body);
				state_ = WRITING;
				return true;
			}
			if (ik == PATH_FORBIDDEN)
			{
				out_ = HttpResponse::buildErrorResponse(403);
				state_ = WRITING;
				return true;
			}
			if (ik == PATH_ERROR)
			{
				out_ = HttpResponse::buildErrorResponse(500);
				state_ = WRITING;
				return true;
			}
			// ik == PATH_MISSING or PATH_DIR -> treat as "no usable index" and continue
		}
		
		// Autoindex if enabled
		if (eff.hasAutoindex && eff.autoindex)
		{
			std::string	listing;
			if (!appendDirectoryListingHtml(listing, uri, path))
			{
				// opendir failed etc. -> forbidden is fine here
				out_ = HttpResponse::buildErrorResponse(403);
				state_ = WRITING;
				return true;
			}
			out_ = HttpResponse::buildResponse(200, "text/html", listing);
			state_ = WRITING;
			return true;
		}

		out_ = HttpResponse::buildErrorResponse(403);
		state_ = WRITING;
		return true;
	}

	// File flow (pk == PATH_FILE)
	{
		std::string	body;
		if (!readFileToString(path, body))
		{
			// stat() already said it's a file; read failing now is unexpected -> 500
			out_ = HttpResponse::buildErrorResponse(500);
			state_ = WRITING;
			return true;
		}
		out_ = HttpResponse::buildResponse(200, guessContentType(path), body);
		state_ = WRITING;
		return true;
	}
}


/*
 *Когда out_ становится пустым после send — ты возвращаешь false, и Server::run() вызывает closeConnection(fd). Это правильно только потому что у тебя в HTTP-ответе Connection: close. Для MVP — нормально. Но когда будешь делать keep-alive — здесь нужно будет переходить обратно в READING, а не закрывать.
 */
bool Connection::onWritable()
{
	if (state_ != WRITING)
		return true;
	if (out_.empty())
		return false;

	ssize_t	n = ::send(fd_, out_.c_str(), out_.size(), 0);
	if (n <= 0)
		return false;

	LOG_DEBUG("fd=%d send bytes=%ld", fd_, (long)n);
	out_.erase(0, n);

	if (out_.empty())
		return false;

	return true;
}

/*
временные допущения (чтобы не усложнять):

Connection: close → один запрос на соединение
containsDotDot грубая
readFileToString грузит всё в память
404 по любой ошибке чтения файла (на самом деле там надо различать 403/404/500, но это позже)
location ещё не участвуют


*/
