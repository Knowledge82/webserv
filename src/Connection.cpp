/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:20:43 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/14 18:28:36 by vdarsuye         ###   ########.fr       */
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
/*	Если хочешь “по‑профи”, следующий шаг после того, как оно заработает:
	вынести readFileToString в отдельный модуль типа FileUtils (и покрыть тестами),
	сделать safeJoin(root, uri) с нормализацией и защитой от ... */

	//Позже этот guard надо будет заменить на:
	//decode → split path → normalize → check “не вышли ли из root”
	bool		containsDotDot(const std::string &s)//временная примитивная защита от .. в URI
	{
		return (s.find("..") != std::string::npos);
	}

	//узнать, является ли путь директорией, не открывая файл
	bool		isDirectory(const std::string &path)
	{
		struct stat	st;
		if (::stat(path.c_str(), &st) != 0)
			return false;
		return S_ISDIR(st.st_mode);
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

	struct	EffectiveConfig
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
			, hasUploadDir(false)
			, uploadDir()
			, hasRedirect(false)
			, redirectCode(0)
			, redirectTarget()
		{
		}
	};

	//проверяет “URI начинается с префикса location” + граница, чтобы /img не съедал /images
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
				bsetLen = prefix.size();
			}
		}
		
		return best;
	}

	//делает merge: server defaults, а потом location overrides
	EffectiveConfig	buildEffectiveConfig(const ServerConfig &srv, const LocationConfig *loc)
	{
		EffectiveConfig	eff;

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

		// autoindex (location-only in our config)
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
			if (eff.allowedMethods[i] = method)
				return true;
		}
		return false;
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

	// ----------------- layer 2 determine limits and parse HTTP --------------------------

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
	
	// ----------------- layer 3 реакция на state парсера --------------------------
	if (st == HttpRequest::ERROR)
	{
		int	status = request_.getErrorStatus();
		out_ = HttpResponse::buildErrorResponse(status);
		state_ = WRITING;				// переключаем состояние
	}
	else if (st == HttpRequest::COMPLETE)
	{
		if (!cfg_ || cfg_->servers.empty())			// валидность cfg
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

		const ServerConfig &srv = cfg_->servers[serverIndex_];

		if (request_.getMethod() != "GET")			// проверяем метод GET
		{
			out_ = HttpResponse::buildErrorResponse(405);
			state_ = WRITING;
			return true;
		}

		if (!srv.hasRoot)							// проверяем, что задан root
		{
			out_ = HttpResponse::buildErrorResponse(500);
			state_ = WRITING;
			return true;
		}
	
		// MAPPING URI -> FILESYSTEM PATH
		const std::string	uri = request_.getUri();

		// Minimal path traversal guard (we'll improve later)
		if (containsDotDot(uri))
		{
			out_ = HttpResponse::buildErrorResponse(403);
			state_ = WRITING;
			return true;
		}

		std::string	path;

		if (uri.empty() || uri[0] != '/')
		{
			out_ = HttpResponse::buildErrorResponse(400);
			state_ = WRITING;
			return true;
		}

		if (uri == "/")
		{
			if (!srv.hasIndex)
			{
				out_ = HttpResponse::buildErrorResponse(403);
				state_ = WRITING;
				return true;
			}
			path = joinPath(srv.root, srv.index);
		}
		else
		{
			// drop leading '/'
			path = joinPath(srv.root, uri.substr(1));

			// if it's a directory, try index
			if (isDirectory(path))
			{
				if (!srv.hasIndex)
				{
					out_ = HttpResponse::buildErrorResponse(403);
					state_ = WRITING;
					return true;
				}
				path = joinPath(path, srv.index);
			}
		}

		std::string	body;
		if (!readFileToString(path, body))
		{
			out_ = HttpResponse::buildErrorResponse(404);
			state_ = WRITING;
			return true;
		}
		
		out_ = HttpResponse::buildResponse(200, guessContentType(path), body);
		state_ = WRITING;
	}

	return true;
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
