/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:20:43 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/28 14:46:36 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Connection.hpp"
#include "Filesystem.hpp"
#include "FilesystemHandler.hpp"
#include "Path.hpp"
#include "EffectiveConfig.hpp"
#include "HttpResponse.hpp"
#include "CgiHandler.hpp"
#include "Log.hpp"

#include <poll.h> //POLLIN/POLLOUT
#include <sys/types.h>
#include <sys/socket.h> //recv/send
#include <unistd.h> // close
#include <vector> // std::vector<std::string> in EffectiveConfig
#include <errno.h>

namespace
{
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

	if (Http::isCgiRequest(loc, uri))
	{
		Http::HttpReply rep = Http::buildCgiReply(eff, loc, request_);
		return prepareReply(rep);
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

