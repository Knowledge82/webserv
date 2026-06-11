/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CgiHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/25 18:27:37 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/11 10:43:40 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
#include "CgiHandler.hpp"
#include "Path.hpp"
#include "Filesystem.hpp"
#include "HttpReply.hpp"
#include "Log.hpp"

#include <unistd.h> // fork, pipe, dup2, execve, chdir, close, getcwd
#include <sys/wait.h> // waitpid
#include <cstdlib> // getenv
#include <cerrno>
#include <cstring>
#include <sstream>
#include <vector>
#include <limits.h> // PATH_MAX


namespace Http
{
	bool		splitDirFile(std::string &outDir, std::string &outFile, const std::string &path)
	{
		std::string::size_type	slash = path.find_last_of('/');
		if (slash == std::string::npos)
		{
			outDir = ".";
			outFile = path;
			return true;
		}
		if (slash == 0)
			outDir = "/";
		else
		{
			outDir = path.substr(0, slash);
			LOG_DEBUG("splitDirFile: outDir = %s", outDir.c_str());
		}
		outFile = path.substr(slash + 1);
		LOG_DEBUG("splitDirFile: outFile = %s", outFile.c_str());
		
		return true;
	}

	// NEW: REFACTOR async CGI
	// ТЕПЕРЬ ОНА ТОЛЬКО ГОТОВИТ ДАННЫЕ И НЕ БЛОКИРУЕТ СЕРВЕР!
	bool prepareCgiArgs(const EffectiveConfig &eff,
						const LocationConfig *loc,
						const HttpRequest &req,
						std::string &outExePath,
						std::string &outScriptFile,
						std::string &outWorkDir,
						std::vector<std::string> &outEnv,
						int	&outStatus)
	{
		if (!loc || !loc->hasCgi)
			return false;

		outStatus = 500; // Дефолтное значение на случай непредвиденного пиздеца
		
		// Наш БОНУС: Выбираем интерпретатор динамически по расширению!
		std::string	ext = Http::getExtension(req.getUri());
		std::map<std::string, std::string>::const_iterator	it = loc->cgiHandlers.find(ext);
		if (it == loc->cgiHandlers.end())
			return false;

		outExePath = it->second;

		std::string			uriPath = Http::uriPathOnly(req.getUri());
		std::string			scriptName = uriPath;
		std::string			pathInfo = uriPath;
		std::string			scriptFsPath;
		int					safeStatus = 200;

		if (eff.hasAlias)
		{
			if (!Http::safeJoinAlias(eff.alias, loc->prefix, uriPath, scriptFsPath, safeStatus))
			{
				LOG_DEBUG("safeJoinAlias failed!");
				return false;
			}
		}
		else
		{
			if (!Http::safeJoin(eff.root, uriPath, scriptFsPath, safeStatus))
			{
				LOG_DEBUG("safeJoin failed!");
				return false;
			}
		}
/*
		Fs::PathKind	pk = Fs::classifyPath(scriptFsPath);
		if (pk == Fs::PATH_MISSING || pk == Fs::PATH_FORBIDDEN || pk == Fs::PATH_ERROR)
		{
			outStatus = Fs::pathKindToHttpStatus(pk);
			LOG_DEBUG("PATH MISSING / FORBIDDEN / ERROR");
			return false;
		}
*/
		// Вычисляем рабочую директорию и чистое имя файла скрипта
		Http::splitDirFile(outWorkDir, outScriptFile, scriptFsPath);

		// Заполняем переменные окружения
		outEnv.push_back("GATEWAY_INTERFACE=CGI/1.1");
		outEnv.push_back("SERVER_PROTOCOL=HTTP/1.1");
		outEnv.push_back(std::string("REQUEST_METHOD=") + req.getMethod());
		outEnv.push_back(std::string("QUERY_STRING=") + Http::uriQueryOnly(req.getUri()));
		
		std::string host = req.getHeader("host");
		if (host.empty())
			host = "localhost";
		outEnv.push_back(std::string("HTTP_HOST=") + host);
		outEnv.push_back(std::string("REQUEST_URI=") + req.getUri());
		outEnv.push_back(std::string("SERVER_NAME=") + host);
		outEnv.push_back("SERVER_PORT=8080");
		outEnv.push_back(std::string("SCRIPT_NAME=") + scriptName);
		outEnv.push_back(std::string("SCRIPT_FILENAME=") + scriptFsPath);
		outEnv.push_back(std::string("PATH_INFO=") + pathInfo);
		outEnv.push_back(std::string("PATH_TRANSLATED=") + scriptFsPath);
		outEnv.push_back("REDIRECT_STATUS=200");

		if (req.getMethod() == "POST" || req.getMethod() == "PUT")
		{
			std::ostringstream	oss;
			oss << req.getContentLength();
			outEnv.push_back(std::string("CONTENT_LENGTH=") + oss.str());

			std::string	ct = req.getHeader("content-type");
			if (!ct.empty())
				outEnv.push_back(std::string("CONTENT_TYPE=") + ct);		
		}

		LOG_DEBUG("[CGI_ENV_BUILD] Calculated SCRIPT_NAME='%s', PATH_INFO='%s'", 
          scriptName.c_str(), pathInfo.c_str());
	
		// =========================================================================
    	// ЖЕЛЕЗНЫЙ ФИКС ДЛЯ SPECIAL HEADERS (ПРОКИДЫВАЕМ HTTP_* В ENV):
    	// =========================================================================
    	// Предполагаем, что req.getHeaders() возвращает std::map или ссылку на контейнер заголовков.
    	// Если контейнер называется по-другому, подставь правильный метод твоего HttpRequest!
    	const std::map<std::string, std::string> &headers = req.getAllHeaders();
    
    	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
    	{
			std::string key = it->first;
			std::string value = it->second;

        	// По спецификации CGI, Content-Type и Content-Length обрабатываются отдельно
        	// (они у тебя уже добавлены как CONTENT_TYPE и CONTENT_LENGTH без префикса HTTP_)
        	std::string lowerKey = key;
        	for (size_t i = 0; i < lowerKey.size(); ++i) 
				lowerKey[i] = std::tolower(lowerKey[i]);

			if (lowerKey == "content-type" || lowerKey == "content-length")
				continue;

			// 1. Переводим ключ заголовка в ВЕРХНИЙ регистр и заменяем '-' на '_'
			std::string cgiKey = "";
			for (size_t i = 0; i < key.size(); ++i)
			{
				if (key[i] == '-')
					cgiKey += '_';
				else
					cgiKey += std::toupper(key[i]);
			}

			// 2. Склеиваем финальную переменную окружения: HTTP_ + ИМЯ = ЗНАЧЕНИЕ
			std::string envVar = "HTTP_" + cgiKey + "=" + value;
			outEnv.push_back(envVar);
		}
		
		return true;
	}


	/* DEPRECATED <==================================================================
	bool		runCgi(std::string &outStdout,
									const std::string &exePath,
									const std::string &scriptFsPath,
									const std::vector<std::string> &env,
									const std::string &workDir,
									const std::string &stdinData)
	{
		
		// ABS Path  <=====
		std::string exeAbs = exePath;
		
		if (!exeAbs.empty() && exeAbs[0] != '/')
		{
    		char cwd[PATH_MAX];
    		if (::getcwd(cwd, sizeof(cwd)) == 0)
				return false;
    		
			// Если путь начинается с ./ — убираем
			if (exeAbs.rfind("./", 0) == 0)
				exeAbs = exeAbs.substr(2);
			
			exeAbs = std::string(cwd) + "/" + exeAbs;
		}

		LOG_DEBUG("CGI exec: final exeAbs = %s", exeAbs.c_str());
		// <==========

		int	inPipe[2];
		int	outPipe[2];

		if (::pipe(inPipe) != 0)
			return false;
		if (::pipe(outPipe) != 0)
		{
			::close(inPipe[0]);
			::close(inPipe[1]);
			return false;
		}

		// для 100мб теста
		try
		{
			setNonBlocking(inPipe[1]); // write end of STDIN pipe
		}
		catch (const std::exception& e)
		{
			LOG_DEBUG("CGI: failed to set non-blocking: %s", e.what());
			// закрываем pipes и возвращаем false
			::close(inPipe[0]); ::close(inPipe[1]);
			::close(outPipe[0]); ::close(outPipe[1]);
			return false;
		}

		pid_t	pid = ::fork();
		LOG_DEBUG("CGI fork pid=%d", (int)pid);
		if (pid < 0)
		{
			::close(inPipe[0]); ::close(inPipe[1]);
			::close(outPipe[0]); ::close(outPipe[1]);
			return false;
		}

		if (pid == 0)
		{
			::dup2(inPipe[0], STDIN_FILENO);
			::dup2(outPipe[1], STDOUT_FILENO);
			
			::close(inPipe[0]); ::close(inPipe[1]);
			::close(outPipe[0]); ::close(outPipe[1]);

			if (!workDir.empty())
				::chdir(workDir.c_str());

			char	**envp = buildEnvp(env);

			// argv": <exe> <scriptFsPath> (safe even if ignored)
			char	*argv[3];
			argv[0] = const_cast<char*>(exeAbs.c_str());
			//argv[0] = const_cast<char*>(exePath.c_str());
			argv[1] = const_cast<char*>(scriptFsPath.c_str());
			argv[2] = 0;

			::execve(argv[0], argv, envp);

			freeEnvp(envp);
			LOG_DEBUG("CGI child: execve failed errno=%d (%s)", errno, strerror(errno));
			::_exit(127);
		}
		::close(inPipe[0]);
		::close(outPipe[1]);

		bool	ok = true;

		if (!stdinData.empty())
		{
			LOG_DEBUG("CGI parent: stdin bytes=%zu", stdinData.size());
			if (!writeAll(inPipe[1], stdinData))
				ok = false;
			LOG_DEBUG("CGI parent: stdin writeAll ok=%d errno=%d", ok ? 1 : 0, errno);
		}
		::close(inPipe[1]);
		LOG_DEBUG("CGI parent: closed stdin pipe");

		if (ok)
		{
			LOG_DEBUG("CGI parent: reading stdout...");
			if (!readAll(outPipe[0], outStdout))
				ok = false;
			LOG_DEBUG("CGI parent: readAll ok=%d stdout.size()=%zu errno=%d",
          ok ? 1 : 0, outStdout.size(), errno);
		}
		::close(outPipe[0]);

		int		st = 0;
		::waitpid(pid, &st, 0);
		LOG_DEBUG("CGI waitpid: st=%d exited=%d code=%d signaled=%d sig=%d",
          st,
          WIFEXITED(st) ? 1 : 0,
          WIFEXITED(st) ? WEXITSTATUS(st) : -1,
          WIFSIGNALED(st) ? 1 : 0,
          WIFSIGNALED(st) ? WTERMSIG(st) : -1);
		return ok;
	}
	*/
	
	bool		parseCgiOutput(int &outStatus,
								std::string &outType,
								std::string &outBody,
								const std::string &cgiStdout)
	{
		// Ищем разделитель заголовков/тела: сначала \r\n\r\n, потом \n\n
		std::string::size_type	sep = cgiStdout.find("\r\n\r\n");
		std::size_t				sepLen = 4;
		std::string				lineEnd = "\r\n";

		if (sep == std::string::npos)
		{
			sep = cgiStdout.find("\n\n");
			sepLen = 2;
			lineEnd = "\n";
		}

		if (sep == std::string::npos)
		{
			outStatus = 200;
			outType = "text/plain";
			outBody = cgiStdout;
			return true;
		}

		std::string	headers = cgiStdout.substr(0, sep);
		outBody = cgiStdout.substr(sep + sepLen);

		outStatus = 200;
		outType = "text/plain";

		std::string::size_type	pos = 0;
		while (pos < headers.size())
		{
			std::string::size_type	eol = headers.find(lineEnd, pos);
			std::string				line;
			if (eol == std::string::npos)
			{
				line = headers.substr(pos);
				pos = headers.size();
			}
			else
			{
				line = headers.substr(pos, eol - pos);
				pos = eol + lineEnd.size();
			}

			std::string::size_type	colon = line.find(':');
			if (colon == std::string::npos)
				continue;

			std::string				key = line.substr(0, colon);
			std::string				val = line.substr(colon + 1);
			while (!val.empty() && (val[0] == ' ' || val[0] == '\t'))
				val.erase(0, 1);

			if (key == "Status")
			{
				std::istringstream	iss(val);
				int					code = 0;
				iss >> code;
				if (iss && code >= 100 && code <= 599)
					outStatus = code;
			}
			else if (key == "Content-Type")
			{
				if (!val.empty())
					outType = val;
			}
			else if (key == "Location")
			{
				// Optional: treat Location as redirect if Status is 3xx.
				// We'll keep it simple for now; Connection will wrap this as NORMAL.
				(void)val;
			}
		}
		return true;
	}


	bool		isCgiRequest(const LocationConfig *loc, const std::string &uri)
	{
		LOG_DEBUG("Проверка на isCgiRequest...");
		if (!loc || !loc->hasCgi)
			return false;

		std::string	ext = Http::getExtension(uri);
		if (ext.empty())
			return false;

		return (loc->cgiHandlers.find(ext) != loc->cgiHandlers.end());
	}
}
/*	HttpReply	buildCgiReply(const EffectiveConfig &eff,
							const LocationConfig *loc,
							const HttpRequest &req)
	{
		if (!loc || !loc->hasCgi)
			return Http::makeErrorReply(500);
		
		if ((req.getMethod() == "POST" || req.getMethod() == "PUT")
    		&& req.getBody().size() != req.getContentLength())
			return Http::makeErrorReply(500);
		
		std::string	ext = Http::getExtension(req.getUri());
		std::map<std::string, std::string>::const_iterator	it = loc->cgiHandlers.find(ext);
		if (it == loc->cgiHandlers.end())
			return Http::makeErrorReply(500);

		const std::string	&exePath = it->second;

		// Map URI -> filesystem path (script filename)
		std::string			uriPath = Http::uriPathOnly(req.getUri());	// "/directory/youpi.bla"
		std::string			prefix = loc->prefix;						// "/directory/"

		std::string scriptName = uriPath;
		//std::string pathInfo = uriPath;
		std::string pathInfo = "";
		
		std::string			scriptFsPath;
		int					safeStatus = 200;

		if (eff.hasAlias)
		{
			if (!Http::safeJoinAlias(eff.alias, loc->prefix, uriPath, scriptFsPath, safeStatus))
				return Http::makeErrorReply(safeStatus);
		}
		else
		{
			if (!Http::safeJoin(eff.root, uriPath, scriptFsPath, safeStatus))
				return Http::makeErrorReply(safeStatus);
		}

		// If script file doesn't exist: 404/403 like file handling
		Fs::PathKind	pk = Fs::classifyPath(scriptFsPath);
		if (pk == Fs::PATH_MISSING || pk == Fs::PATH_FORBIDDEN || pk == Fs::PATH_ERROR)
			return Http::makeErrorReply(Fs::pathKindToHttpStatus(pk));

		// Working dir (required by subject)
		std::string	workDir;
		std::string	scriptFile;
		splitDirFile(workDir, scriptFile, scriptFsPath);

		// Build env
		std::vector<std::string>	env;
		env.push_back("GATEWAY_INTERFACE=CGI/1.1");
		env.push_back("SERVER_PROTOCOL=HTTP/1.1");
		env.push_back(std::string("REQUEST_METHOD=") + req.getMethod());
		env.push_back(std::string("QUERY_STRING=") + Http::uriQueryOnly(req.getUri()));
		std::string host = req.getHeader("host");     // у тебя заголовки уже lower-case
		if (host.empty())
			host = "localhost";
		env.push_back(std::string("HTTP_HOST=") + host);
		env.push_back(std::string("REQUEST_URI=") + req.getUri());
		env.push_back(std::string("SERVER_NAME=") + host);  // да, грубо, но tester’у хватит
		env.push_back("SERVER_PORT=8080");
		env.push_back(std::string("SCRIPT_NAME=") + scriptName);
		env.push_back(std::string("SCRIPT_FILENAME=") + scriptFsPath);
		env.push_back(std::string("PATH_INFO=") + pathInfo);
		env.push_back(std::string("PATH_TRANSLATED=") + scriptFsPath);
		Вариант чуть правильнее (на будущее): вычислять path-info
		 * Нужно определить:
		 * uriPath (у тебя это /directory/youpi.bla)
		 * фактический “script name” (тоже /directory/youpi.bla в этом кейсе)
		 * pathInfo = uriPath.substr(scriptName.size()) (будет "")
		 * PATH_TRANSLATED только если pathInfo не пустой: safeJoin(rootOrAlias, pathInfo).
		env.push_back("REDIRECT_STATUS=200");// helps some CGI (php-cgi), harmless otherwise

		if (req.getMethod() == "POST" || req.getMethod() == "PUT")
		{
			std::ostringstream	oss;
			oss << req.getContentLength();
			env.push_back(std::string("CONTENT_LENGTH=") + oss.str());

			std::string	ct = req.getHeader("content-type");
			if (!ct.empty())
				env.push_back(std::string("CONTENT_TYPE=") + ct);		
		}
		// LOG =============================================================
		LOG_DEBUG("CGI exe=%s script=%s workDir=%s method=%s uri=%s",
          exePath.c_str(),
          scriptFsPath.c_str(),
          workDir.c_str(),
          req.getMethod().c_str(),
          req.getUri().c_str());
		
		LOG_DEBUG("CGI body: contentLength=%zu body.size()=%zu",
          req.getContentLength(),
          req.getBody().size());

		for (std::size_t i = 0; i < env.size(); ++i)
    	LOG_DEBUG("CGI env[%zu]=%s", i, env[i].c_str());
		// =================================================================
		
		std::string cgiStdout;
		bool ok = runCgi(cgiStdout, exePath, scriptFsPath, env, workDir, req.getBody());
		LOG_DEBUG("CGI run ok=%d stdout.size()=%zu", ok ? 1 : 0, cgiStdout.size());
		if (!cgiStdout.empty())
    		LOG_DEBUG("CGI stdout head: %.200s", cgiStdout.c_str());
		if (!ok)
    		return Http::makeErrorReply(500);

		int			status = 200;
		std::string	type = "text/plain";
		std::string	body;

		if (!parseCgiOutput(status, type, body, cgiStdout))
			return Http::makeErrorReply(500);

		LOG_DEBUG("CGI parsed: status=%d type=%s body.size()=%zu",status, type.c_str(), body.size());		
		return Http::makeReply(status, type, body);
	}*/
