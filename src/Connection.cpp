/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:20:43 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/04 10:59:20 by vdarsuye         ###   ########.fr       */
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

#include <poll.h>		//POLLIN/POLLOUT
#include <sys/wait.h>	//waitpid
#include <sys/types.h>
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
	std::string	toCgiHttpHeaderKey(const std::string &lowerKey)
	{
		std::string out = "HTTP_";
		for (std::size_t i = 0; i < lowerKey.size(); ++i)
		{
			char c = lowerKey[i];
			if (c >= 'a' && c <= 'z')
				c = static_cast<char>(c - 'a' + 'A');
			else if (c == '-')
				c = '_';
			out.push_back(c);
		}
		return out;
	}

	bool		parseCgiOutput(int &outStatus,
								std::string &outType,
								std::string &outBody,
								const std::string &cgiStdout)
	{
		std::string::size_type sep = cgiStdout.find("\r\n\r\n");
		if (sep == std::string::npos)
		{
			outStatus = 200;
			outType = "text/plain";
			outBody = cgiStdout;
			return true;
		}

		std::string headers = cgiStdout.substr(0, sep);
		outBody = cgiStdout.substr(sep + 4);

		outStatus = 200;
		outType = "text/plain";

		std::string::size_type pos = 0;
		while (pos < headers.size())
		{
			std::string::size_type eol = headers.find("\r\n", pos);
			std::string line;
			if (eol == std::string::npos)
			{
				line = headers.substr(pos);
				pos = headers.size();
			}
			else
			{
				line = headers.substr(pos, eol - pos);
				pos = eol + 2;
			}

			std::string::size_type colon = line.find(':');
			if (colon == std::string::npos)
				continue;

			std::string key = line.substr(0, colon);
			std::string val = line.substr(colon + 1);
			while (!val.empty() && (val[0] == ' ' || val[0] == '\t'))
				val.erase(0, 1);

			if (key == "Status")
			{
				std::istringstream iss(val);
				int code = 0;
				iss >> code;
				if (iss && code >= 100 && code <= 599)
					outStatus = code;
			}
			else if (key == "Content-Type")
			{
				if (!val.empty())
					outType = val;
			}
		}
		LOG_DEBUG("CGI parsed: status=%d type='%s' body.size()=%zu",
				outStatus, outType.c_str(), outBody.size());
		return true;
	}

	char **buildEnvp(const std::vector<std::string> &env)
	{
		char **envp = new char*[env.size() + 1];
		for (std::size_t i = 0; i < env.size(); ++i)
		{
			envp[i] = new char[env[i].size() + 1];
			std::strcpy(envp[i], env[i].c_str());
		}
		envp[env.size()] = 0;
		return envp;
	}

	void freeEnvp(char **envp)
	{
		if (!envp)
			return;
		for (std::size_t i = 0; envp[i]; ++i)
			delete[] envp[i];
		delete[] envp;
	}

	void splitDirFile(std::string &outDir, std::string &outFile, const std::string &path)
	{
		std::string::size_type slash = path.find_last_of('/');
		if (slash == std::string::npos)
		{
			outDir = ".";
			outFile = path;
			return;
		}
		if (slash == 0)
			outDir = "/";
		else
			outDir = path.substr(0, slash);
		outFile = path.substr(slash + 1);
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
	LOG_DEBUG("prepareReply: RESPONSE FIRST LINE: '%s' out_.size()=%zu", firstLine.c_str(), out_.size());
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
	if (state_ == WRITING && !out_.empty())//нас интересует: “можно ли сейчас писать в сокет”
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

bool	Connection::onReadable()
{
	// ----------------- layer 1: network read --------------------------

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

	// ----------------- layer 2: determine limits and parse HTTP --------------------------

	const std::size_t	maxHeaderBytes = 16 * 1024; // 16KB
	std::size_t			maxBodyBytes = 666 * 1024 * 1024; //default for now 666MB or
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
	
	LOG_DEBUG("PARSE RESULT: st=%d method=%s uri=%s CL=%zu TE='%s' in_.size()=%zu body.size()=%zu",
          (int)st,
          request_.getMethod().c_str(),
          request_.getUri().c_str(),
          request_.getContentLength(),
          request_.getHeader("transfer-encoding").c_str(),
          in_.size(),
          request_.getBody().size());
	
	// ----------------- layer 3: react to parser state --------------------------
	
	if (st == HttpRequest::ERROR)
	{
		int	status = request_.getErrorStatus();
		out_ = HttpResponse::buildErrorResponse(status);
		state_ = WRITING;
		LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply", request_.getMethod().c_str(), request_.getUri().c_str());
		return true;
	}
	
	if (st == HttpRequest::BODY)
	{
		// We have headers parsed (method/uri/content-length known),
		// but body not fully received yet. Apply location-level body limit early.		
		if (!cfg_ || cfg_->servers.empty() || serverIndex_ >= cfg_->servers.size())
		{
			out_ = HttpResponse::buildErrorResponse(500);
			state_ = WRITING;
			LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply", request_.getMethod().c_str(), request_.getUri().c_str());
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
			LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply", request_.getMethod().c_str(), request_.getUri().c_str());
			return true;
		}

		return true; // keep reading until COMPLETE
	}

	if (st != HttpRequest::COMPLETE)
		return true;
	
	// ----------------- layer 4: validate config --------------------------

	if (!cfg_ || cfg_->servers.empty())
	{
		out_ = HttpResponse::buildErrorResponse(500);
		state_ = WRITING;
		LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply", request_.getMethod().c_str(), request_.getUri().c_str());
		return true;
	}
	if (serverIndex_ >= cfg_->servers.size())	// serverIndex в диапазоне
	{
		out_ = HttpResponse::buildErrorResponse(500);
		state_ = WRITING;
		//LOG_DEBUG("STATE -> WRITING because ... (st=%d method=%s uri=%s)", (int)st, ...);
		LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply", request_.getMethod().c_str(), request_.getUri().c_str());
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
		//LOG_DEBUG("STATE -> WRITING because ... (st=%d method=%s uri=%s)", (int)st, ...);
		LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply", request_.getMethod().c_str(), request_.getUri().c_str());
		return true;
	}

	if (tryRedirectToSlashLocation(srv, loc, uri))
		return true;

	if (eff.hasRedirect)
	{
		out_ = HttpResponse::buildRedirectResponse(eff.redirectCode, eff.redirectTarget);
		state_ = WRITING;
	//	LOG_DEBUG("STATE -> WRITING because ... (st=%d method=%s uri=%s)", (int)st, ...);
		LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply", request_.getMethod().c_str(), request_.getUri().c_str());
		return true;
	}

	// Method policy
	if (!isAllowedMethod(request_.getMethod(), eff))
	{
		out_ = HttpResponse::buildErrorResponse(405);
		state_ = WRITING;
		//LOG_DEBUG("STATE -> WRITING because ... (st=%d method=%s uri=%s)", (int)st, ...);
		LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply", request_.getMethod().c_str(), request_.getUri().c_str());
		return true;
	}

	if (!eff.hasAlias && !eff.hasRoot)
	{
		out_ = HttpResponse::buildErrorResponse(500);
		state_ = WRITING;
		//LOG_DEBUG("STATE -> WRITING because ... (st=%d method=%s uri=%s)", (int)st, ...);
		LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "prepareReply", request_.getMethod().c_str(), request_.getUri().c_str());
		return true;
	}

	// ====================== SPECIAL CASE: /post_body ======================
	if (request_.getUri() == "/post_body" || request_.getUri() == "/post_body/")
	{
		if (request_.getMethod() != "POST")
		{
			prepareReply(Http::makeErrorReply(405)); // Method Not Allowed
			return true;
		}

		// Можно вернуть что угодно: 200 + пустое тело, или тестовую строку
		Http::HttpReply rep = Http::makeReply(200, "text/plain", "post_body ok");
		// или даже просто 200 без тела:
		// rep = Http::makeReply(200, "text/plain", "");

		LOG_DEBUG("STATE -> WRITING because %s; method=%s uri=%s", "post_body 200", request_.getMethod().c_str(), uri.c_str());
		return prepareReply(rep);
	
	}
	// ===============================================================================
	
	if (Http::isCgiRequest(loc, uri))
	{
		return startCgi(eff, loc, request_);
	}
	Http::HttpReply rep = Http::buildFileSystemReply(eff, loc, uri);
	return prepareReply(rep);
}


 /* Когда out_ становится пустым после send — ты возвращаешь false, и Server::run() вызывает closeConnection(fd). Это правильно только потому что у тебя в HTTP-ответе Connection: close. Пока нормально. Но когда будешь делать keep-alive — здесь нужно будет переходить обратно в READING, а не закрывать.
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

	LOG_DEBUG("onWritable: fd=%d send bytes=%ld", fd_, (long)n);
	out_.erase(0, n);

	if (out_.empty())
		return false;

	return true;
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
	if (!loc || !loc->hasCgi)
	{
		prepareReply(Http::makeErrorReply(500));
		return true;
	}

	// resolve handler by extension
	std::string ext = Http::getExtension(req.getUri());
	std::map<std::string, std::string>::const_iterator it = loc->cgiHandlers.find(ext);
	if (it == loc->cgiHandlers.end())
	{
		prepareReply(Http::makeErrorReply(500));
		return true;
	}
	const std::string exePath = it->second;

	// Build absolute path for CGI executable because child does chdir(workDir)
	std::string exeAbs = exePath;
	if (!exeAbs.empty() && exeAbs[0] != '/')
	{
		char cwd[PATH_MAX];
		if (::getcwd(cwd, sizeof(cwd)) == 0)
		{
			prepareReply(Http::makeErrorReply(500));
			return true;
		}

		// strip leading "./"
		if (exeAbs.rfind("./", 0) == 0)
			exeAbs = exeAbs.substr(2);

		exeAbs = std::string(cwd) + "/" + exeAbs;
	}
	
	// map uri -> fs script path
	std::string uriPath = Http::uriPathOnly(req.getUri());
	std::string scriptFsPath;
	int safeStatus = 200;

	if (eff.hasAlias)
	{
		if (!Http::safeJoinAlias(eff.alias, loc->prefix, uriPath, scriptFsPath, safeStatus))
		{
			prepareReply(Http::makeErrorReply(safeStatus));
			return true;
		}
	}
	else
	{
		if (!Http::safeJoin(eff.root, uriPath, scriptFsPath, safeStatus))
		{
			prepareReply(Http::makeErrorReply(safeStatus));
			return true;
		}
	}

	Fs::PathKind pk = Fs::classifyPath(scriptFsPath);
	if (pk == Fs::PATH_FORBIDDEN)
	{
		prepareReply(Http::makeErrorReply(403));
		return true;
	}
	if (pk == Fs::PATH_ERROR)
	{
		prepareReply(Http::makeErrorReply(500));
		return true;
	}
	// PATH_MISSING is allowed for CGI tests: continue

	// compute workdir
	std::string workDir;
	std::string scriptFile;
	splitDirFile(workDir, scriptFile, scriptFsPath);

	// build env
	std::vector<std::string> env;
	env.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env.push_back("SERVER_PROTOCOL=HTTP/1.1");
	env.push_back(std::string("REQUEST_METHOD=") + req.getMethod());
	env.push_back(std::string("QUERY_STRING=") + Http::uriQueryOnly(req.getUri()));

	std::string host = req.getHeader("host");
	if (host.empty())
		host = "localhost";
	env.push_back(std::string("HTTP_HOST=") + host);
	env.push_back(std::string("REQUEST_URI=") + req.getUri());
	env.push_back(std::string("SERVER_NAME=") + host);
	env.push_back("SERVER_PORT=8080");

	// SCRIPT_NAME/PATH_INFO — оставь как у тебя сейчас, чтобы tester не ломался
	env.push_back(std::string("SCRIPT_NAME=") + uriPath);
	env.push_back(std::string("PATH_INFO=") + uriPath);

	env.push_back(std::string("SCRIPT_FILENAME=") + scriptFsPath);
	env.push_back(std::string("PATH_TRANSLATED=") + scriptFsPath);
	env.push_back("REDIRECT_STATUS=200");

	if (req.getMethod() == "POST" || req.getMethod() == "PUT")
	{
		std::ostringstream oss;
		oss << req.getContentLength();
		env.push_back(std::string("CONTENT_LENGTH=") + oss.str());
		std::string ct = req.getHeader("content-type");
		if (!ct.empty())
			env.push_back(std::string("CONTENT_TYPE=") + ct);
	}
	// Forward all request headers as CGI variables: HTTP_<NAME>
	// headers_ keys are lower-case in your parser
	{
		const std::map<std::string, std::string> &hdrs = req.getAllHeaders();
		for (std::map<std::string, std::string>::const_iterator hit = hdrs.begin();
		     hit != hdrs.end();
		     ++hit)
		{
			const std::string &k = hit->first;
			const std::string &v = hit->second;

			// Skip headers that have dedicated CGI variables
			if (k == "content-type" || k == "content-length")
				continue;

			// You already set HTTP_HOST explicitly; avoid duplicates
			if (k == "host")
				continue;

			env.push_back(toCgiHttpHeaderKey(k) + "=" + v);
		}
	}
	for (std::size_t i = 0; i < env.size(); ++i)
	LOG_DEBUG("CGI env[%zu]=%s", i, env[i].c_str());

	// create pipes
	int inPipe[2];
	int outPipe[2];
	if (::pipe(inPipe) != 0)
	{
		prepareReply(Http::makeErrorReply(500));
		return true;
	}
	if (::pipe(outPipe) != 0)
	{
		::close(inPipe[0]); ::close(inPipe[1]);
		prepareReply(Http::makeErrorReply(500));
		return true;
	}

	// parent will use: write end of inPipe, read end of outPipe
	// make them non-blocking
	try
	{
		setNonBlocking(inPipe[1]);
		setNonBlocking(outPipe[0]);
	}
	catch (...)
	{
		::close(inPipe[0]); ::close(inPipe[1]);
		::close(outPipe[0]); ::close(outPipe[1]);
		prepareReply(Http::makeErrorReply(500));
		return true;
	}

	/*
	//=========== NEW LIMIT OF CGI's
	if (Server::activeCgiCount >= 7)
	{
		LOG_DEBUG("CGI concurrency limit reached (%d)", Server::activeCgiCount);
		prepareReply(Http::makeErrorReply(503)); // Service Unavailable
		return true;
	}

	Server::activeCgiCount++;
	//==============================
*/
	pid_t pid = ::fork();
	if (pid < 0)
	{
		::close(inPipe[0]); ::close(inPipe[1]);
		::close(outPipe[0]); ::close(outPipe[1]);
		prepareReply(Http::makeErrorReply(500));
		return true;
	}

	if (pid == 0)
	{
		::dup2(inPipe[0], STDIN_FILENO);
		::dup2(outPipe[1], STDOUT_FILENO);

		::close(inPipe[0]); ::close(inPipe[1]);
		::close(outPipe[0]); ::close(outPipe[1]);

		if (!workDir.empty())
			::chdir(workDir.c_str());

		char **envp = buildEnvp(env);

		// NOTE: exe path as is (если надо — делай абсолютный заранее)
		char *argv[3];
		argv[0] = const_cast<char*>(exeAbs.c_str());
		argv[1] = const_cast<char*>(scriptFsPath.c_str());
		argv[2] = 0;

		::execve(argv[0], argv, envp);
		freeEnvp(envp);
		::_exit(127);
	}

	// parent
	::close(inPipe[0]);
	::close(outPipe[1]);
	
	cgiDeadline_ = std::time(0) + 120; //таймаут 2 min
	
	// arm connection CGI state
	cgiPid_ = pid;
	cgiStdinFd_ = inPipe[1];
	cgiStdoutFd_ = outPipe[0];

	cgiStdinClosed_ = false;
	cgiStdoutClosed_ = false;

	cgiInData_ = req.getBody();    // <-- копия body
	cgiInOffset_ = 0;
	
	cgiOut_.clear();

	state_ = CGI;

	// Parent: if we have nothing to send to CGI stdin, close it immediately (send EOF)
	if (cgiInData_.empty() && !cgiStdinClosed_ && cgiStdinFd_ >= 0)
	{
		::close(cgiStdinFd_);
		cgiStdinFd_ = -1;
		cgiStdinClosed_ = true;
		LOG_DEBUG("CGI: closed stdin immediately (empty body)");
	}

	LOG_DEBUG("STATE -> CGI; clientFd=%d cgiPid=%d inFd=%d outFd=%d body=%zu",
	          fd_, (int)cgiPid_, cgiStdinFd_, cgiStdoutFd_, cgiInData_.size());
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

	// if both sides are closed, finalize
	if (cgiStdinClosed_ && cgiStdoutClosed_)
	{
		int st = 0;
		if(cgiPid_ > 0)
		{
			::waitpid(cgiPid_, &st, 0);
			cgiPid_ = -1;
		}
	//	Server::activeCgiCount--;

		// на всякий случай обнуляем fd чтобы buildPollFds их больше не подхватил
		cgiStdinFd_ = -1;
		cgiStdoutFd_ = -1;
		int status = 200;
		std::string type = "text/plain";
		std::string body;
		LOG_DEBUG("CGI raw stdout size=%zu", cgiOut_.size());
		LOG_DEBUG("CGI raw stdout head: %.200s", cgiOut_.c_str());
		if (!parseCgiOutput(status, type, body, cgiOut_))
		{
			LOG_DEBUG("CGI body head: %.80s", body.c_str());
			prepareReply(Http::makeErrorReply(500));
		}
		else
		{
			LOG_DEBUG("CGI body head: %.80s", body.c_str());
			prepareReply(Http::makeReply(status, type, body));
		}

		// prepareReply sets WRITING
		return true;
	}

	return true;
}
