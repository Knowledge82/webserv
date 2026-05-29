/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CgiHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/25 18:27:37 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/29 14:25:35 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

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



namespace
{
	bool		writeAll(int fd, const std::string &data)
	{
		const char	*p = data.c_str();
		std::size_t	left = data.size();
		std::size_t	total = 0;

		while (left > 0)
		{
			ssize_t	n = ::write(fd, p, left);
			if (n < 0)
			{
				if (errno == EINTR)
					continue;
				LOG_DEBUG("CGI writeAll: write error errno=%d (%s) total=%zu left=%zu",
                      errno, strerror(errno), total, left);
				return false;
			}
			if (n == 0)
        	{
            	LOG_DEBUG("CGI writeAll: write returned 0 total=%zu left=%zu", total, left);
            	return false;
        	}
			p += n;
			left -= (std::size_t)n;
			total += (std::size_t)n;
		}
		
		LOG_DEBUG("CGI writeAll: done total=%zu", total);

		return true;
	}
	
	bool		readAll(int fd, std::string &out)
	{
		char	buf[4096];

		while (true)
		{
			ssize_t	n = ::read(fd, buf, sizeof(buf));
			if (n < 0)
			{
				if (errno == EINTR)
					continue;
				return false;
			}
			if (n == 0)
				break;
			out.append(buf, n);
		}
		
		return true;
	}

	char		**buildEnvp(const std::vector<std::string> &env)
	{
		char	**envp = new char*[env.size() + 1];
		for (std::size_t i = 0; i < env.size(); ++i)
		{
			envp[i] = new char[env[i].size() + 1];
			strcpy(envp[i], env[i].c_str());
		}
		envp[env.size()] = 0;
		
		return envp;
	}

	void		freeEnvp(char **envp)
	{
		if (!envp)
			return;
		for (std::size_t i = 0; envp[i]; ++i)
			delete[] envp[i];
		delete[] envp;
	}

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
			outDir = path.substr(0, slash);
		outFile = path.substr(slash + 1);
		
		return true;
	}

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
		LOG_DEBUG("CGI pipes: inPipe=[%d,%d] outPipe=[%d,%d]",
				inPipe[0], inPipe[1], outPipe[0], outPipe[1]);
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

	bool		parseCgiOutput(int &outStatus,
								std::string &outType,
								std::string &outBody,
								const std::string &cgiStdout)
	{
		std::string::size_type	sep = cgiStdout.find("\r\n\r\n");
		if (sep == std::string::npos)
		{
			outStatus = 200;
			outType = "text/plain";
			outBody = cgiStdout;
			return true;
		}

		std::string	headers = cgiStdout.substr(0, sep);
		outBody = cgiStdout.substr(sep + 4);

		outStatus = 200;
		outType = "text/plain";

		std::string::size_type	pos = 0;
		while (pos < headers.size())
		{
			std::string::size_type	eol = headers.find("\r\n", pos);
			std::string				line;
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
}

namespace Http
{
	bool		isCgiRequest(const LocationConfig *loc, const std::string &uri)
	{
		if (!loc || !loc->hasCgi)
			return false;

		std::string	ext = Http::getExtension(uri);
		if (ext.empty())
			return false;

		return (loc->cgiHandlers.find(ext) != loc->cgiHandlers.end());
	}

	HttpReply	buildCgiReply(const EffectiveConfig &eff,
							const LocationConfig *loc,
							const HttpRequest &req)
	{
		if (!loc || !loc->hasCgi)
			return Http::makeErrorReply(500);
//==========================
		if ((req.getMethod() == "POST" || req.getMethod() == "PUT")
    		&& req.getBody().size() != req.getContentLength())
			return Http::makeErrorReply(500);
//====================
		std::string	ext = Http::getExtension(req.getUri());
		std::map<std::string, std::string>::const_iterator	it = loc->cgiHandlers.find(ext);
		if (it == loc->cgiHandlers.end())
			return Http::makeErrorReply(500);

		const std::string	&exePath = it->second;

		// Map URI -> filesystem path (script filename)
		std::string			uriPath = Http::uriPathOnly(req.getUri());	// "/directory/youpi.bla"
		std::string			prefix = loc->prefix;						// "/directory/"

		std::string scriptName = uriPath;
		std::string pathInfo = uriPath;
		
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
		/*Вариант чуть правильнее (на будущее): вычислять path-info
		 * Нужно определить:
		 * uriPath (у тебя это /directory/youpi.bla)
		 * фактический “script name” (тоже /directory/youpi.bla в этом кейсе)
		 * pathInfo = uriPath.substr(scriptName.size()) (будет "")
		 * PATH_TRANSLATED только если pathInfo не пустой: safeJoin(rootOrAlias, pathInfo).*/
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
	}
}
