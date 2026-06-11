/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:20:43 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/11 12:22:11 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Connection.hpp"
#include "Server.hpp"
#include "FdUtils.hpp"
#include "Filesystem.hpp"
#include "FilesystemHandler.hpp"
#include "Path.hpp"
#include "EffectiveConfig.hpp"
#include "HttpResponse.hpp"
#include "CgiHandler.hpp"
#include "Log.hpp"
#include "Mime.hpp"

#include <poll.h>		//POLLIN/POLLOUT
#include <sys/wait.h>	//waitpid
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h> //recv/send
#include <limits.h>		// PATH_MAX
#include <unistd.h>		// pipe, fork, dup2, close, chdir
#include <fcntl.h>		// fcntl (если надо)
#include <signal.h>		// kill
#include <vector>		// std::vector<std::string> in EffectiveConfig
#include <cerrno>
#include <cstring>
#include <sstream>


// ============================================ UTILS ================================

namespace
{
	char **buildEnvp(const std::vector<std::string> &env)
	{
		LOG_DEBUG("[CGI_DEBUG] buildEnvp called with %zu elements", env.size());
		char **envp = new char*[env.size() + 1];
		for (std::size_t i = 0; i < env.size(); ++i)
		{
			envp[i] = new char[env[i].size() + 1];
			std::strcpy(envp[i], env[i].c_str());
		}
		envp[env.size()] = 0;
		return envp;
	}

	const LocationConfig	*selectLocation(const std::vector<LocationConfig> &locations,
											const std::string &uri)
	{
		const LocationConfig	*best = NULL;
		std::size_t				bestLen = 0;

		for (std::size_t i = 0; i < locations.size(); ++i)
		{
			const LocationConfig	&loc = locations[i];
			const std::string		&prefix = loc.prefix;

			if (!Http::startsWithPrefix(uri, prefix))
				continue;

			if (prefix.size() >= bestLen)
			{
				best = &loc;
			bestLen = prefix.size();
			}
		}
		
		return best;
	}

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

		// alias (location-only)
		if (loc && loc->hasAlias)
		{
			eff.hasAlias= true;
			eff.alias = loc->alias;
		}//правило: если alias задан, root в eff можно игнорить для маппинга
		 //(но не обязательно переписывать eff.root).

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

		// client_max_body_size
		if (srv.hasClientMaxBodySize)
		{
			eff.hasClientMaxBodySize = true;
			eff.clientMaxBodySize = srv.clientMaxBodySize;
		}
		if (loc && loc->hasClientMaxBodySize)
		{
			eff.hasClientMaxBodySize = true;
			eff.clientMaxBodySize = loc->clientMaxBodySize;
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
		
		// CGI
		if (loc && loc->hasCgi)
		{
			eff.hasCgi = true;
			eff.cgiHandlers = loc->cgiHandlers; // карта расширений.
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
}


// ========================================= CONNECTION ==============================

Connection::Connection() // по факту может и не нужен, но оставить можно
	: fd_(-1)
	, state_(Connection::READING)
	, cfg_(NULL)
	, serverIndex_(0)
	, cgiPid_(-1)
	, cgiStdinFd_(-1)
	, cgiStdoutFd_(-1)
	, cgiInOffset_(0)
	, cgiInData_()
	, cgiOut_()
	, cgiStdinClosed_(true)
	, cgiStdoutClosed_(true)
	, cgiDeadline_(0)
	, fileStreamFd_(-1)
	, fileStreamBytesLeft_(0)
{
}

Connection::Connection(int fd, const Config *cfg, std::size_t serverIndex) // main constructor
	: fd_(fd)
	, state_(Connection::READING)
	, cfg_(cfg) // cfg_ нужен, чтобы достать root/index/max_body_size
	, serverIndex_(serverIndex) //нужен, чтобы выбрать правильный server block
	, cgiPid_(-1)
	, cgiStdinFd_(-1)
	, cgiStdoutFd_(-1)
	, cgiInOffset_(0)
	, cgiInData_()
	, cgiOut_()
	, cgiStdinClosed_(true)
	, cgiStdoutClosed_(true)
	, cgiDeadline_(0)
	, fileStreamFd_(-1)
	, fileStreamBytesLeft_(0)
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

bool Connection::prepareReply(const Http::HttpReply &r)
{
	if (r.kind == Http::REPLY_REDIRECT)
		out_ = HttpResponse::buildRedirectResponse(r.redirectCode, r.location);
	else if (r.kind == Http::REPLY_ERROR)
		out_ = HttpResponse::buildErrorResponse(r.status);
	else
		out_ = HttpResponse::buildResponse(r.status, r.contentType, r.body);

	//=================== LOG
	std::string::size_type eol = out_.find("\r\n");
	std::string firstLine = (eol == std::string::npos) ? out_ : out_.substr(0, eol);
	
	LOG_DEBUG("prepareReply called! CURRENT STATE BEFORE REPLY = %d", state_);
	LOG_DEBUG("prepareReply: KIND = %d, STATUS = %d, BODY SIZE = %zu", 
          static_cast<int>(r.kind), r.status, r.body.size());	
	//====================
	
	state_ = WRITING;
	LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply", request_.getMethod().c_str(), request_.getUri().c_str());
	return true;
}

short	Connection::wantedPollEvents() const
{
	//“какие события нам нужны от poll”. Connection говорит Server что ему нужно от poll.
	//Очень важная идея: Server не должен знать протокол, он просто выполняет то, что Connection просит.
	short	ev = 0; // пока ничего не хотим. В реальном сервере обычно так не делают, но для MVP пойдёт.
	if (state_ == READING)//при READING ты просишь poll: “разбуди меня, когда будет что читать”
		ev = ev | POLLIN;
	if (state_ == WRITING && (!out_.empty() || fileStreamFd_ >= 0))//нас интересует: “можно ли сейчас писать в сокет”
		ev = ev | POLLOUT;//POLLOUT означает: в сокете есть место в буфере отправки, send скорее всего не заблокируется. Но только если out_ реально содержит данные. Если out_ пуст — писать нечего, значит мы не просим POLLOUT
	
	return ev;
}


bool	Connection::tryRedirectToSlashLocation(const ServerConfig &srv,
									const LocationConfig *loc,
									const std::string &uri)
{

	if ((request_.getMethod() != "GET") /* || request_.getMethod() == "HEAD" */)
		return false;

	if (Http::endsWithSlash(uri))
		return false;

	const LocationConfig *locSlash = selectLocation(srv.locations, uri + "/");
	if (!locSlash)
		return false;
	
	if (locSlash && (loc && locSlash->prefix.size() <= loc->prefix.size()))
		return false;
	
	EffectiveConfig effSlash = buildEffectiveConfig(srv, locSlash);

	if (!effSlash.hasAlias && !effSlash.hasRoot)
		return false;

	std::string	p;
	int s = 200;

	if (effSlash.hasAlias)
	{
		if (!Http::safeJoinAlias(effSlash.alias, locSlash->prefix, uri + "/", p, s))
			return false;
	}
	else
	{
		if (!Http::safeJoin(effSlash.root, uri + "/", p, s))
			return false;
	}
	
	if (Fs::classifyPath(p) != Fs::PATH_DIR)
		return false;

	out_ = HttpResponse::buildRedirectResponse(301, uri + "/");
	state_ = WRITING;
	LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply", request_.getMethod().c_str(), request_.getUri().c_str());
	return true;	
}

// ======================================= DELETE =====================================

bool Connection::handleDelete(const EffectiveConfig &eff)
{
	std::string	path;
	int			safeStatus = 200;
	std::string uri = request_.getUri();

	// 1. Строим полный путь к файлу на диске ровно так же, как это делается в FilesystemHandler.cpp
	if (eff.hasAlias)
	{
		const ServerConfig	&srv = cfg_->servers[serverIndex_];
		const LocationConfig *loc = selectLocation(srv.locations, uri);
		if (!loc || !Http::safeJoinAlias(eff.alias, loc->prefix, uri, path, safeStatus))
		{
			prepareReply(Http::makeErrorReply(safeStatus));
			return true;
		}
	}
	else
	{
		if (!Http::safeJoin(eff.root, uri, path, safeStatus))
		{
			prepareReply(Http::makeErrorReply(safeStatus));
			return true;
		}
	}

	LOG_DEBUG("DELETE: mapped path is '%s'", path.c_str());

	// 2. Классифицируем полученный путь с помощью твоего Fs модуля
	Fs::PathKind pk = Fs::classifyPath(path);

	// Если файла нет — 404. Если нет прав доступа к каталогу/файлу — 403.
	if (pk == Fs::PATH_MISSING || pk == Fs::PATH_FORBIDDEN || pk == Fs::PATH_ERROR)
	{
		prepareReply(Http::makeErrorReply(Fs::pathKindToHttpStatus(pk)));
		return true;
	}

	// 3. Защита по RFC: Обычный HTTP DELETE не должен удалять директории (для этого есть WebDAV RMDIR).
	// Если клиент пытается удалить папку, кидаем 403 Forbidden.
	if (pk == Fs::PATH_DIR)
	{
		LOG_DEBUG("DELETE: path '%s' is a directory. Forbidden.", path.c_str());
		prepareReply(Http::makeErrorReply(403));
		return true;
	}

	// 4. Пытаемся удалить регулярный файл через системный вызов
	if (::unlink(path.c_str()) != 0)
	{
		LOG_DEBUG("DELETE: unlink failed for '%s', errno=%d", path.c_str(), errno);
		if (errno == EACCES || errno == EPERM)
			prepareReply(Http::makeErrorReply(403));
		else
			prepareReply(Http::makeErrorReply(500));
		return true;
	}

	// 5. Успешно удалено. Формируем красивый ответ 200 OK.
	LOG_INFO("DELETE: successfully removed file '%s'", path.c_str());
	prepareReply(Http::makeReply(200, "text/plain", "File successfully deleted.\n"));
	return true;
}

// ======================================= UPLOAD =====================================

bool Connection::handleUpload(const EffectiveConfig &eff, const LocationConfig *loc)
{
	(void)eff;
	
	// 1. Проверяем наличие директивы upload_store / upload_dir
	if (!loc || !loc->hasUploadDir || loc->uploadDir.empty())
	{
		LOG_DEBUG("UPLOAD: upload_store directive is missing in this location");
		prepareReply(Http::makeErrorReply(500));
		return true;
	}

	std::string uri = request_.getUri();
	
	// 2. Выделяем имя файла из URI
	std::size_t lastSlash = uri.find_last_of('/');
	std::string filename;
	if (lastSlash != std::string::npos && lastSlash < uri.size() - 1)
	{
		filename = uri.substr(lastSlash + 1);
	}

	// Если имя пустое, генерируем по старинке (time() возвращает time_t, в C++98 приводим через оstringstream)
	if (filename.empty())
	{
		std::ostringstream oss;
		oss << "upload_" << ::time(NULL) << ".tmp";
		filename = oss.str();
	}

	// 3. Собираем финальный путь
	std::string finalPath = Fs::joinPath(loc->uploadDir, filename);
	LOG_DEBUG("UPLOAD: Attempting to save file to: '%s'", finalPath.c_str());

	// 4. Открываем файл
	int fileFd = ::open(finalPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fileFd < 0)
	{
		LOG_DEBUG("UPLOAD: Failed to open/create file '%s'", finalPath.c_str());
		prepareReply(Http::makeErrorReply(500));
		return true;
	}

	// 5. ЖЕЛЕЗОБЕТОННЫЙ ЦИКЛ ЗАПИСИ (Защита от частичной записи по завету второй сучки)
	const std::string &body = request_.getBody();
	if (!body.empty())
	{
		const char*	ptr = body.data();
		std::size_t	bytesLeft = body.size();

		while (bytesLeft > 0)
		{
			ssize_t written = ::write(fileFd, ptr, bytesLeft);
			if (written < 0)
			{
				LOG_DEBUG("UPLOAD: write() failed");
				::close(fileFd);
				::unlink(finalPath.c_str()); // Удаляем недописанный мусор
				prepareReply(Http::makeErrorReply(500));
				return true;
			}
			
			// Сдвигаем указатель и уменьшаем счетчик оставшихся байт
			ptr += written;
			bytesLeft -= static_cast<std::size_t>(written);
		}
	}

	// 6. Закрываем файл
	::close(fileFd);
	LOG_INFO("UPLOAD: Successfully saved file '%s' (size=%zu bytes)", finalPath.c_str(), body.size());

	// 7. Отвечаем 201 Created
	prepareReply(Http::makeReply(201, "text/plain", "File uploaded successfully.\n"));
	return true;
}

// ======================================= STREAMING (SENDING_FILE)=====================================

bool Connection::handleStartSendingFile(const std::string &filePath, std::size_t fileSize)
{
	// 1. Открываем файл на чтение
	fileStreamFd_ = ::open(filePath.c_str(), O_RDONLY);
	if (fileStreamFd_ < 0)
	{
		LOG_DEBUG("SENDING_FILE: Failed to open file '%s'", filePath.c_str());
		out_ = HttpResponse::buildErrorResponse(500);
		state_ = WRITING;
		return true;
	}

	fileStreamBytesLeft_ = fileSize;

	// 2. Генерируем только заголовки ответа
	std::ostringstream oss;
	oss << "HTTP/1.1 200 OK\r\n";
	oss << "Content-Type: " << Http::guessContentType(filePath) << "\r\n";
	oss << "Content-Length: " << fileSize << "\r\n";
	oss << "Connection: close\r\n"; // Закрываем сокет после окончания стриминга
	oss << "\r\n";

	out_ = oss.str();
	
	// ВАЖНО: Остаемся в стандартном состоянии WRITING! 
	// Server.cpp будет думать, что это обычная отправка данных.
	state_ = WRITING; 
	
	LOG_DEBUG("SENDING_FILE: Sub-streaming initialized for '%s', size=%zu. Headers packed into out_.", 
	          filePath.c_str(), fileSize);
	return true;
}
// ======================================= ONREADABLE =====================================

bool	Connection::onReadable()
{
	LOG_DEBUG("ЗАЛЕТЕЛИ В onReadable");

	char	buf[4096];
	ssize_t	n = ::recv(fd_, buf, sizeof(buf), 0);
	if (n == 0) // клиент закрыл соединение
	{
		LOG_DEBUG("onReadable: EOF from client; attempting final parse with in_.size=%zu", in_.size());
		return false;
	}
	if (n < 0) // ошибка
		return false;

	in_.append(buf, n);

	const std::size_t	maxHeaderBytes = 16 * 1024; // 16KB
	std::size_t			maxBodyBytes = 666 * 1024 * 1024; //default for now 666MB or
	if (cfg_ && serverIndex_ < cfg_->servers.size())
	{
		const ServerConfig	&srv = cfg_->servers[serverIndex_];
		if (srv.hasClientMaxBodySize)
			maxBodyBytes = srv.clientMaxBodySize;
	}
	
	HttpRequest::State	st = request_.parse(in_, maxHeaderBytes, maxBodyBytes);
	
	LOG_DEBUG("st=%d method=%s uri=%s CL=%zu in_.size()=%zu body.size()=%zu",
          (int)st,
          request_.getMethod().c_str(),
          request_.getUri().c_str(),
          request_.getContentLength(),
          in_.size(),
          request_.getBody().size());
	
	if (st == HttpRequest::ERROR)
	{
		int	status = request_.getErrorStatus();
		out_ = HttpResponse::buildErrorResponse(status);
		state_ = WRITING;
		LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply",
				request_.getMethod().c_str(), request_.getUri().c_str());
		return true;
	}
	
	if (st == HttpRequest::BODY)
	{
		if (!cfg_ || cfg_->servers.empty() || serverIndex_ >= cfg_->servers.size())
		{
			out_ = HttpResponse::buildErrorResponse(500);
			state_ = WRITING;
			LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply",
					request_.getMethod().c_str(), request_.getUri().c_str());
			return true;
		}
		const ServerConfig		&srv = cfg_->servers[serverIndex_];
		const std::string		uri = request_.getUri();
	
		const LocationConfig	*loc = selectLocation(srv.locations, request_.getUri());
		EffectiveConfig			eff = buildEffectiveConfig(srv, loc);

		if (eff.hasClientMaxBodySize && request_.getContentLength() > eff.clientMaxBodySize)
		{
			out_ = HttpResponse::buildErrorResponse(413);
			state_ = WRITING;
			LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply",
					request_.getMethod().c_str(), request_.getUri().c_str());
			return true;
		}
	
		return true;
	}

	if (st == HttpRequest::COMPLETE)
	{
		LOG_DEBUG("==========> st == COMPLETE");
		if (!cfg_ || cfg_->servers.empty() || serverIndex_ >= cfg_->servers.size())
		{
			out_ = HttpResponse::buildErrorResponse(500);
			state_ = WRITING;
			LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply",
					request_.getMethod().c_str(), request_.getUri().c_str());
			return true;
		}
	
		const ServerConfig		&srv = cfg_->servers[serverIndex_];
		const std::string		uri = request_.getUri();
	
		const LocationConfig	*loc = selectLocation(srv.locations, request_.getUri());
		EffectiveConfig			eff = buildEffectiveConfig(srv, loc);

		if (eff.hasClientMaxBodySize && request_.getContentLength() > eff.clientMaxBodySize)
		{
			out_ = HttpResponse::buildErrorResponse(413);
			state_ = WRITING;
			LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply",
					request_.getMethod().c_str(), request_.getUri().c_str());
			return true;
		}

		if (tryRedirectToSlashLocation(srv, loc, uri))
			return true;

		if (eff.hasRedirect)
		{
			out_ = HttpResponse::buildRedirectResponse(eff.redirectCode, eff.redirectTarget);
			state_ = WRITING;
			LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply",
					request_.getMethod().c_str(), request_.getUri().c_str());
			return true;
		}

		// Method policy (ВРЕМЕННЫЙ ХАК ДЛЯ ТЕСТА DELETE) <======================
		if (request_.getMethod() != "DELETE" && !isAllowedMethod(request_.getMethod(), eff))
		{
			out_ = HttpResponse::buildErrorResponse(405);
			state_ = WRITING;
			LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply",
					request_.getMethod().c_str(), request_.getUri().c_str());
			return true;
		}

		if (!eff.hasAlias && !eff.hasRoot)
		{
			out_ = HttpResponse::buildErrorResponse(500);
			state_ = WRITING;
			LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply",
					request_.getMethod().c_str(), request_.getUri().c_str());
			return true;
		}
		
		// ====================== НОВЫЙ БЛОК: МЕТОД DELETE ======================
		if (request_.getMethod() == "DELETE")
		{
			LOG_DEBUG("onReadable: Handling DELETE request for URI: %s", uri.c_str());
			return handleDelete(eff); // Наш метод удаления
		}
		
		// ====================== METHOD: UPLOAD (POST/PUT) ======================
		// Проверяем, что метод POST или PUT, и у этого локейшна включена директива upload
		if ((request_.getMethod() == "POST" || request_.getMethod() == "PUT")
				&& loc && loc->hasUploadDir)
		{
			LOG_DEBUG("onReadable: Handling simple UPLOAD request for URI: %s", uri.c_str());
			return handleUpload(eff, loc);
		}
		
		// ====================== SPECIAL CASE: /post_body ======================
		if (request_.getUri() == "/post_body" || request_.getUri() == "/post_body/")
		{
			if (request_.getMethod() != "POST")
			{
				prepareReply(Http::makeErrorReply(405)); // Method Not Allowed
				return true;
			}

			Http::HttpReply rep = Http::makeReply(200, "text/plain", "post_body ok");
	
			LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "post_body 200",
					request_.getMethod().c_str(), uri.c_str());
			return prepareReply(rep);
		}
		
		// =============================== CGI ==========================================
	
		if (Http::isCgiRequest(loc, uri))
		{
			LOG_DEBUG("Это CGI. Запускается startCgi()...");
			startCgi(eff, loc, request_);
			return true;
		}
		
		// =============================== SENDING_FILE ==========================================
		// [Внутри Connection::onReadable() перед вызовом buildFileSystemReply]
		if (request_.getMethod() == "GET")
		{
			std::string filePath;
			int safeStatus = 200;
			
			// Маппим URI в путь на диске с помощью твоих хелперов
			if (eff.hasAlias) {
				if (loc) Http::safeJoinAlias(eff.alias, loc->prefix, uri, filePath, safeStatus);
			} else {
				Http::safeJoin(eff.root, uri, filePath, safeStatus);
			}

			// Классифицируем путь
			Fs::PathKind pk = Fs::classifyPath(filePath);
			if (pk == Fs::PATH_FILE)
			{
				struct stat st;
				// Используем совет сучки: берем метаданные БЕЗ чтения файла
				if (::stat(filePath.c_str(), &st) == 0)
				{
					std::size_t fileSize = st.st_size;
					
					// Устанавливаем порог. Например, файлы больше 500 КБ стримим,
					// а мелкие index.html пусть отдает старый быстрый buildFileSystemReply
					if (fileSize > 500 * 1024) 
					{
						LOG_INFO("onReadable: File '%s' is large (%zu bytes). Activating SENDING_FILE streaming.", 
						         filePath.c_str(), fileSize);
						return handleStartSendingFile(filePath, fileSize);
					}
				}
			}
		}

		// Твой стандартный дефолтный код для мелких файлов, директорий и автоиндекса
		Http::HttpReply rep = Http::buildFileSystemReply(eff, loc, uri);
		return prepareReply(rep);
	}

	return true;
}



// ======================================= ONWRITABLE =====================================
bool Connection::onWritable()
{
	if (state_ != WRITING)
		return true;
	
	// ---- ФАЗА 1: Отправка текстового буфера out_ (заголовки или мелкие ответы/автоиндекс) ----
	if (!out_.empty())
	{
		ssize_t n = ::send(fd_, out_.c_str(), out_.size(), 0);
		if (n <= 0)
			return false;

		LOG_DEBUG("onWritable (WRITING Headers/Data): fd=%d send bytes=%ld", fd_, (long)n);
		out_.erase(0, n);
		
		// Если в out_ еще что-то осталось, выходим до следующего POLLOUT
		if (!out_.empty())
			return true;

		// ВОТ ОНО! Если out_ стал ПУСТЫМ, проверяем: 
		// Если стриминга файла нет (fileStreamFd_ < 0), значит мы только что 
		// ПОЛНОСТЬЮ отправили мелкий файл, ошибку или АВТОИНДЕКС! 
		// Возвращаем false, чтобы сервер закрыл это Connection.
		if (fileStreamFd_ < 0)
		{
			LOG_DEBUG("onWritable: Short response (or autoindex) fully sent. Closing connection.");
			return false;
		}
		
		// Если же fileStreamFd_ >= 0, значит ушли только заголовки большого файла.
		// Не выходим, а сразу проваливаемся ниже в Фазу 2, чтобы отправить первый чанк!
	}

	// ---- ФАЗА 2: ИНКРЕМЕНТАЛЬНЫЙ СТРИМИНГ БОЛЬШОГО ФАЙЛА С ДИСКА ----
	if (fileStreamFd_ >= 0)
	{
		if (fileStreamBytesLeft_ == 0)
		{
			::close(fileStreamFd_); fileStreamFd_ = -1;
			return false;
		}

		char buf[4096];
		ssize_t bytesRead = ::read(fileStreamFd_, buf, sizeof(buf));
		if (bytesRead < 0)
		{
			LOG_DEBUG("SENDING_FILE: read error from file fd=%d", fileStreamFd_);
			::close(fileStreamFd_); fileStreamFd_ = -1;
			return false;
		}
		if (bytesRead == 0)
		{
			LOG_DEBUG("SENDING_FILE: Unexpected EOF");
			::close(fileStreamFd_); fileStreamFd_ = -1;
			return false;
		}

		ssize_t bytesSent = ::send(fd_, buf, bytesRead, 0);
		if (bytesSent < 0)
		{
			LOG_DEBUG("SENDING_FILE: send error to client fd=%d", fd_);
			::close(fileStreamFd_); fileStreamFd_ = -1;
			return false;
		}

		LOG_DEBUG("SENDING_FILE: fd=%d sent chunk size=%ld, total left=%zu", 
		          fd_, (long)bytesSent, fileStreamBytesLeft_);

		if (bytesSent < bytesRead)
		{
			off_t offset = bytesSent - bytesRead; 
			::lseek(fileStreamFd_, offset, SEEK_CUR);
		}

		fileStreamBytesLeft_ -= static_cast<std::size_t>(bytesSent);

		if (fileStreamBytesLeft_ == 0)
		{
			LOG_INFO("SENDING_FILE: File transfer successfully finished.");
			::close(fileStreamFd_);
			fileStreamFd_ = -1;
			return false; // Стриминг завершен, закрываем сокет клиентов
		}
		return true; // Файл еще не закончился, ждем следующий POLLOUT
	}

	// Сюда код дойти не должен, но для безопасности возвращаем false
	return false;
}
// ======================================= CGI PART CONNECTION =====================================

bool Connection::hasCgi() const
{
	return (cgiPid_ > 0 || cgiStdinFd_ >= 0 || cgiStdoutFd_ >= 0);
}

int Connection::getCgiStdinFd() const
{
	return cgiStdinFd_;
}

int Connection::getCgiStdoutFd() const
{
	return cgiStdoutFd_;
}

short Connection::wantedCgiStdinEvents() const
{
	if (state_ != CGI)
		return 0;
	if (cgiStdinFd_ < 0)
		return 0;
	if (cgiStdinClosed_)
		return 0;
	if (cgiInOffset_ >= cgiInData_.size()) // cgiInData_ = request body copy/buffer
		return 0;
	return POLLOUT;
}

short Connection::wantedCgiStdoutEvents() const
{
	if (state_ != CGI)
		return 0;
	if (cgiStdoutFd_ < 0)
		return 0;
	if (cgiStdoutClosed_)
		return 0;
	return POLLIN;
}

void Connection::closeAllFdsAndKillCgiIfAny()
{
	if (cgiStdinFd_ >= 0)
	{
		::close(cgiStdinFd_);
		cgiStdinFd_ = -1;
	}
	if (cgiStdoutFd_ >= 0)
	{
		::close(cgiStdoutFd_);
		cgiStdoutFd_ = -1;
	}
	cgiStdinClosed_ = true;
	cgiStdoutClosed_ = true;

	if (cgiPid_ > 0)
	{
		::kill(cgiPid_, SIGKILL);
		::waitpid(cgiPid_, 0, 0);
		cgiPid_ = -1;
	}
}

bool Connection::startCgi(const EffectiveConfig &eff,
                          const LocationConfig *loc,
                          const HttpRequest &req)
{
    std::string exePath, scriptFile, workDir;
    std::vector<std::string> cgiEnv;
	int	cgiStatus = 500;

	if (state_ == CGI)
	{
        LOG_DEBUG("startCgi() called again while already in CGI state — ignoring");
        return true;
    }	
	// ========================== LOGS =================================
    static int callCount = 0;
    callCount++;
    LOG_DEBUG("=== startCgi() CALLED! Счётчик = %d, ТЕКУЩИЙ СТЕЙТ CONNECTION = %d ===", callCount, state_);
	
	// 1. Готовим аргументы. Если пути невалидны или лимиты нарушены — сразу бьём 500 ошибку
    LOG_DEBUG("[CGI_DEBUG] Entering prepareCgiArgs: URI='%s', Method='%s'", 
              req.getUri().c_str(), req.getMethod().c_str());
	if (!Http::prepareCgiArgs(eff, loc, req, exePath, scriptFile, workDir, cgiEnv, cgiStatus))
    {
        LOG_DEBUG("[CGI_DEBUG] prepareCgiArgs failed! Launching prepareReply with status %d", cgiStatus);
		prepareReply(Http::makeErrorReply(cgiStatus));
        state_ = WRITING;
        return false;
    }

	// ЛОГ ПОСЛЕ ВЫЗОВА: смотрим, какие пути получились
    LOG_DEBUG("[CGI_DEBUG] prepareCgiArgs SUCCESS:");
    LOG_DEBUG("  exePath    = '%s'", exePath.c_str());
    LOG_DEBUG("  scriptFile = '%s'", scriptFile.c_str());
    LOG_DEBUG("  workDir    = '%s'", workDir.c_str());
    LOG_DEBUG("  Env Count  = %zu", cgiEnv.size());

	// Выводим ТУПО ВЕСЬ список переменных, который улетит в тестер!
    for (size_t i = 0; i < cgiEnv.size(); ++i)
    {
        LOG_DEBUG("  ENV[%zu] = '%s'", i, cgiEnv[i].c_str());
    }
	//========================================================
	
    // 2. Создаем пайпы для общения с процессом
    int inPipe[2];
    int outPipe[2];
    if (::pipe(inPipe) < 0 || ::pipe(outPipe) < 0)
    {
        prepareReply(Http::makeErrorReply(500));
        state_ = WRITING;
        return false;
    }

    // Делаем дескрипторы со стороны веб-сервера НЕБЛОКИРУЮЩИМИ для poll()
    setNonBlocking(inPipe[1]);
    setNonBlocking(outPipe[0]);

    // 3. Форкаемся!
    pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(inPipe[0]); ::close(inPipe[1]);
        ::close(outPipe[0]); ::close(outPipe[1]);
        prepareReply(Http::makeErrorReply(500));
        state_ = WRITING;
        return false;
    }

    if (pid == 0)
    {
        // МЫ В ДОЧЕРНЕМ ПРОЦЕССЕ (Здесь execve, он заменяет тело процесса)
        ::close(inPipe[1]);  
        ::close(outPipe[0]); 

        ::dup2(inPipe[0], STDIN_FILENO);
        ::dup2(outPipe[1], STDOUT_FILENO);
        ::close(inPipe[0]);
        ::close(outPipe[1]);

        // Переводим путь интерпретатора в абсолютный
        std::string exeAbs = exePath;
        if (!exeAbs.empty() && exeAbs[0] != '/')
        {
            char cwd[PATH_MAX];
            if (::getcwd(cwd, sizeof(cwd)) != 0)
            {
				std::string currentDir(cwd);
                if (exeAbs.rfind("./", 0) == 0)
					exeAbs = exeAbs.substr(2);
                exeAbs = currentDir + "/" + exeAbs;
            }
        }

        std::string scriptAbsPath = scriptFile; 
        if (!workDir.empty() && (scriptFile.empty() || scriptFile[0] != '/'))
            scriptAbsPath = workDir + "/" + scriptFile;
       
		if (!workDir.empty())
            ::chdir(workDir.c_str());

        char **envp = buildEnvp(cgiEnv);
        
		char *argv[3];
        argv[0] = const_cast<char*>(exeAbs.c_str());
       // argv[1] = const_cast<char*>(scriptAbsPath.c_str()); 
        argv[1] = const_cast<char*>(scriptFile.c_str()); // <========== try to fix cgi path for py
        argv[2] = 0;

        ::execve(argv[0], argv, envp);
        ::_exit(127); // Если execve сдох, аварийно выходим
    }

    // МЫ В РОДИТЕЛЬСКОМ ПРОЦЕССЕ (ВЕБ-СЕРВЕР)
    ::close(inPipe[0]);  
    ::close(outPipe[1]);

    // Задаем дедлайн для CGI (120 секунд), чтобы спастись от зависших скриптов
    cgiDeadline_ = std::time(0) + 120;

    // Сохраняем неблокирующие fds в переменные класса Connection
    cgiStdinFd_  = inPipe[1];
    cgiStdoutFd_ = outPipe[0];
    cgiPid_      = pid;
    
    // Загружаем тело запроса во внутренний буфер для постепенной отправки через poll
    cgiInData_   = req.getBody(); 
    cgiInOffset_ = 0;
    cgiOut_.clear(); // <--- ФИКС: Очищаем именно тот буфер, в который пишем в onCgiEvent!
    
    cgiStdinClosed_  = false;
    cgiStdoutClosed_ = false;

    // ПЕРЕКЛЮЧАЕМ СТЭЙТ-МАШИНУ НА АСИНХРОННЫЙ CGI
    state_ = CGI;

    // ФИКС: Если тело запроса пустое (GET-запрос), закрываем stdin сразу, 
    // чтобы скрипт понял, что данных на вход больше не будет и начал выполняться!
    if (cgiInData_.empty() && !cgiStdinClosed_ && cgiStdinFd_ >= 0)
    {
        ::close(cgiStdinFd_);
        cgiStdinFd_ = -1;
        cgiStdinClosed_ = true;
        LOG_DEBUG("CGI: closed stdin immediately (empty body)");
    }
    
    LOG_DEBUG("CGI launched asynchronously: pid=%d, state -> CGI", pid);
    return true;
}

bool Connection::onCgiEvent(int fd, short revents)
{
	if (state_ != CGI)
		return true;

	if (cgiDeadline_ != 0 && std::time(0) > cgiDeadline_)
	{
		// timeout
		LOG_DEBUG("!!! CGI TIMEOUT !!! fd=%d pid=%d stdout.size=%zu time waited=%ld sec",
              fd_, cgiPid_, cgiOut_.size(), std::time(0) - (cgiDeadline_ - 120));
		prepareReply(Http::makeErrorReply(504)); // или 500 если не хочешь 504
		state_ = WRITING;
		closeAllFdsAndKillCgiIfAny();
		return true;
	}

	// POLLHUP is NOT automatically fatal for pipes
	// we still want to drain stdout if possible (do not early-return here)
	if (revents & (POLLERR | POLLNVAL))
	{
		prepareReply(Http::makeErrorReply(500));
		state_ = WRITING;
		closeAllFdsAndKillCgiIfAny();
		return true;
	}

	// stdout readable
	if (fd == cgiStdoutFd_ && (revents & (POLLIN | POLLHUP)))
	{
		char buf[4096];
		while (true)
		{
			ssize_t n = ::read(cgiStdoutFd_, buf, sizeof(buf));
			if (n > 0)
			{
				cgiOut_.append(buf, n);
				continue;
			}
			if (n == 0)
			{
				::close(cgiStdoutFd_);
				cgiStdoutFd_ = -1;
				cgiStdoutClosed_ = true;
				break;
			}
			// n < 0
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			
			
			LOG_DEBUG("CGI stdin write ERROR: n=%zd, errno=%d (%s), cgiInOffset=%zu", 
			          n, errno, ::strerror(errno), cgiInOffset_);
			prepareReply(Http::makeErrorReply(500));
			state_ = WRITING;
			closeAllFdsAndKillCgiIfAny();
			return true;
		}
	}

	// stdin writable
	if (fd == cgiStdinFd_ && (revents & POLLOUT))
	{
		while (cgiInOffset_ < cgiInData_.size())
		{
			const char *p = cgiInData_.c_str() + cgiInOffset_;
			std::size_t left = cgiInData_.size() - cgiInOffset_;
			ssize_t n = ::write(cgiStdinFd_, p, left);
			if (n > 0)
			{
				cgiInOffset_ += static_cast<std::size_t>(n);
				continue;
			}
			if (n < 0 && errno == EINTR)
				continue;
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
				break;

			prepareReply(Http::makeErrorReply(500));
			state_ = WRITING;
			closeAllFdsAndKillCgiIfAny();
			return true;
		}

		// done writing -> close stdin to signal EOF to CGI
		if (cgiInOffset_ >= cgiInData_.size() && !cgiStdinClosed_)
		{
			::close(cgiStdinFd_);
			cgiStdinFd_ = -1;
			cgiStdinClosed_ = true;
		}
	}

	// =========================================================================
	// ЖЕЛЕЗНЫЙ ФИКС ДЛЯ 100-МЕГАБАЙТНОГО ТЕСТА (ВСТАВЛЯТЬ СЮДА):
	// =========================================================================
	if (cgiStdoutClosed_ && !cgiStdinClosed_)
	{
		if (cgiStdinFd_ >= 0)
		{
			::close(cgiStdinFd_);
			cgiStdinFd_ = -1;
		}
		cgiStdinClosed_ = true;
		LOG_DEBUG("CGI: script finished early. Force closed stdin pipe to finalize response.");
	}

	/* OLD DEPRECATED <=============================================================
	// if both sides are closed, finalize
	if (cgiStdinClosed_ && cgiStdoutClosed_)
	{
		int st = 0;
		if(cgiPid_ > 0)
		{
			::waitpid(cgiPid_, &st, 0);
			cgiPid_ = -1;
		}

		// на всякий случай обнуляем fd чтобы buildPollFds их больше не подхватил
		cgiStdinFd_ = -1;
		cgiStdoutFd_ = -1;
		int status = 200;
		std::string type = "text/plain";
		std::string body;
		if (!Http::parseCgiOutput(status, type, body, cgiOut_))
			prepareReply(Http::makeErrorReply(500));
		else
			prepareReply(Http::makeReply(status, type, body));

		// prepareReply sets WRITING
		return true;
	}
	*/
	// if both sides are closed, finalize
	if (cgiStdinClosed_ && cgiStdoutClosed_)
	{
		int st = 0;
		bool processFailed = false;

		if (cgiPid_ > 0)
		{
			::waitpid(cgiPid_, &st, 0);

			// ПРОВЕРЯЕМ СТАТУС ЗАВЕРШЕНИЯ ПРОЦЕССА БЛИН!
			if (WIFEXITED(st))
			{
				int exitCode = WEXITSTATUS(st);
				if (exitCode != 0)
				{
					LOG_DEBUG("CGI process pid=%d exited with ERROR status=%d", cgiPid_, exitCode);
					processFailed = true;
				}
			}
			else if (WIFSIGNALED(st))
			{
				LOG_DEBUG("CGI process pid=%d was KILLED by signal=%d", cgiPid_, WTERMSIG(st));
				processFailed = true;
			}

			cgiPid_ = -1;
		}

		// на всякий случай обнуляем fd чтобы buildPollFds их больше не подхватил
		cgiStdinFd_ = -1;
		cgiStdoutFd_ = -1;

		// Если процесс явно сдох — сразу шлем 500 без всякого парсинга пустоты!
		if (processFailed)
		{
			prepareReply(Http::makeErrorReply(500));
			state_ = WRITING;
			return true;
		}

		// Если процесс завершился нормально (exit code 0), то парсим его выхлоп
		int status = 200;
		std::string type = "text/plain";
		std::string body;

		if (!Http::parseCgiOutput(status, type, body, cgiOut_))
		{
			prepareReply(Http::makeErrorReply(500));
		}
		else
		{
			prepareReply(Http::makeReply(status, type, body));
		}

		// prepareReply sets WRITING
		return true;
	}

	return true;
}

