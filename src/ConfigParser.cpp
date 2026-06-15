/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigParser.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/07 11:28:47 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/26 16:36:08 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ConfigParser.hpp"

#include <sstream>// std::ostringstream
#include <stdexcept>
#include <limits>

namespace
{
	std::runtime_error	parseError(const Tokenizer::Token &tok, const std::string &msg)
	{
		std::ostringstream	oss;
	
		oss << "config parse error at line " << tok.line << ", col " << tok.col << ": " << msg;//build error message in stream buffer, each << appends

		return std::runtime_error(oss.str());// return a copy of the buffer as a string
	}

	int					parsePortStrict(const std::string &s, const Tokenizer::Token &tok)
	//Parse string into int and validate:
	//that the entire string is a number (no "8080abc")
	//range 1..65535
	{
		std::istringstream	iss(s);
		int					p = -1;
		char				extra;

		iss >> p;
		if (!iss || (iss >> extra) || p < 1 || p > 65535)
			throw parseError(tok, "invalid port: " + s);

		return p;
	}

	/*
	 *Parse client_max_body_size as size_t.
	 Why unsigned long internally? Because C++98 has no stoull, and size_t can be 32 or 64-bit.
	 We read into a sufficiently wide type, then compare with numeric_limits<size_t>::max()
	 */
	std::size_t			parseSizeTStrict(const std::string &s, const Tokenizer::Token &tok)
	{
		std::istringstream	iss(s);
		unsigned long		v = 0;
		char				extra;
	
		iss >> v;
		if (!iss || (iss >> extra))
			throw parseError(tok, "invalid number: " + s);
		if (v > static_cast<unsigned long>(std::numeric_limits<std::size_t>::max()))
			throw parseError(tok, "number too large: " + s);

		return static_cast<std::size_t>(v);
	}

	int					parseIntStrict(const std::string &s, const Tokenizer::Token &tok)
	{
		std::istringstream	iss(s);
		int					v = 0;
		char				extra;
	
		iss >> v;
		if (!iss || (iss >> extra))
			throw parseError(tok, "invalid integer: " + s);

		return v;
	}
}

// ==================== PARSER ========================

//when creating the parser, we immediately "take" the first token,
//otherwise nextToken_ would contain garbage and parseConfig() would need separate initialization
ConfigParser::ConfigParser(const std::string &path)
	: tokenizer_(path)
	, nextToken_(tokenizer_.next())
{
}

void						ConfigParser::consumeToken()
{
	nextToken_ = tokenizer_.next();
}

void						ConfigParser::expect(Tokenizer::TokenType t, const char *description)
{
	if (nextToken_.type != t)
		throw parseError(nextToken_, std::string("expected ") + description);
	consumeToken();
}

bool						ConfigParser::isWord(const char *w) const
{
	return (nextToken_.type == Tokenizer::T_WORD && nextToken_.text == w);
}

std::vector<std::string>	ConfigParser::readArgsUntilSemi()
{
	std::vector<std::string>	args;

	while (nextToken_.type != Tokenizer::T_SEMI)//while current token is not ;
	{
		if (nextToken_.type == Tokenizer::T_EOF || nextToken_.type == Tokenizer::T_LBRACE || nextToken_.type == Tokenizer::T_RBRACE)
			// if we see {/}/EOF — syntax error (directive can't suddenly become a block)
			throw parseError(nextToken_, "expected ';' after directive");
		if (nextToken_.type != Tokenizer::T_WORD) // must be WORD (argument) 
			throw parseError(nextToken_, "expected argument");
		args.push_back(nextToken_.text);
		consumeToken();
	}
	expect(Tokenizer::T_SEMI, "';'");
	
	return args;
	// invariant: nextToken_ is at the first token after ;
}

Config						ConfigParser::parseConfig()
{
	Config	cfg;

	while (nextToken_.type != Tokenizer::T_EOF) // until EOF
	{
		if (!isWord("server")) 					// expect the word server
			throw parseError(nextToken_, "only 'server' blocks are allowed at top-level");

		consumeToken();
		cfg.servers.push_back(parseServer());	// parse server block
	}

	if (cfg.servers.empty())					// 0 servers is an error
		throw parseError(nextToken_, "no server blocks found");

	return cfg;
}

ServerConfig				ConfigParser::parseServer()
{
	ServerConfig	srv;

	expect(Tokenizer::T_LBRACE, "'{' after server");

	while (nextToken_.type != Tokenizer::T_RBRACE)
	{
		if (nextToken_.type == Tokenizer::T_EOF)
			throw parseError(nextToken_, "unexpected end of file inside server block");

		if (isWord("location"))
		{
			consumeToken();
			srv.locations.push_back(parseLocation());
		}
		else
			parseServerDirective(srv);
	}

	expect(Tokenizer::T_RBRACE, "'}' to close server block");

	if (srv.listens.empty())
	{
		ListenConfig	l;
		srv.listens.push_back(l);
	}

	return srv;
}

LocationConfig				ConfigParser::parseLocation()
{
	//location cannot contain nested location or server blocks — language limitation.
	//Sufficient for the subject.
	LocationConfig	loc;

	if (nextToken_.type != Tokenizer::T_WORD) // expect WORD - prefix
		throw parseError(nextToken_, "location requiers a prefix");
	
	loc.prefix = nextToken_.text;
	consumeToken();

	expect(Tokenizer::T_LBRACE, "'{' after location prefix"); // expect {

	while (nextToken_.type != Tokenizer::T_RBRACE)
	{
		if (nextToken_.type == Tokenizer::T_EOF)
			throw parseError(nextToken_, "unexpected end of file inside location block");
		parseLocationDirective(loc);
	}

	expect(Tokenizer::T_RBRACE, "'}' to close location block");

	return loc;
}

void						ConfigParser::parseServerDirective(ServerConfig &srv)
{
	if (nextToken_.type != Tokenizer::T_WORD)
		throw parseError(nextToken_, "expected directive name");
	Tokenizer::Token	nameTok = nextToken_;
	consumeToken();
	
	std::vector<std::string>	args = readArgsUntilSemi();
	
	applyServerDirective(nameTok, args, srv);
}

void						ConfigParser::parseLocationDirective(LocationConfig &loc)
{
	if (nextToken_.type != Tokenizer::T_WORD)
		throw parseError(nextToken_, "expected directive name");

	Tokenizer::Token	nameTok = nextToken_;
	consumeToken();

	std::vector<std::string>	args = readArgsUntilSemi();
	applyLocationDirective(nameTok, args, loc);
}

void						ConfigParser::applyServerDirective(const Tokenizer::Token &nameTok,
																const std::vector<std::string> &args,
																ServerConfig &srv)
{
	const std::string	&name = nameTok.text;

	if (name == "listen")
	{
		if (args.size() != 1)
			throw parseError(nameTok, "listen expects 1 argument (host:port)");

		std::string::size_type	pos = args[0].find(':');
		if (pos == std::string::npos)
			throw parseError(nameTok, "listen must be host:port");

		ListenConfig	l;
		l.host = args[0].substr(0, pos);
		l.port = parsePortStrict(args[0].substr(pos + 1), nameTok);
		srv.listens.push_back(l);
		return;
	}
	if (name == "root")
	{
		if (args.size() != 1)
			throw parseError(nameTok, "root expects 1 argument");

		srv.hasRoot = true;
		srv.root = args[0];
		return;
	}
	if (name == "index")
	{
		if (args.size() != 1)
			throw parseError(nameTok, "index expects 1 argument");

		srv.hasIndex= true;
		srv.index = args[0];
		return;
	}
	if (name == "autoindex")
	{
		if (args.size() != 1)
			throw parseError(nameTok, "autoindex expects 1 argument: on|off");

		srv.hasAutoindex = true;
		if (args[0] == "on")
			srv.autoindex= true;
		else if (args[0] == "off")
			srv.autoindex= false;
		else
			throw parseError(nameTok, "autoindex expects 'on' or 'off'");
		return;
	}
	if (name == "client_max_body_size")
	{
		if (args.size() != 1)
			throw parseError(nameTok, "client_max_body_size expects 1 argument");

		srv.hasClientMaxBodySize= true;
		srv.clientMaxBodySize = parseSizeTStrict(args[0], nameTok);
		return;
	}
	if (name == "error_page")
	{
		if (args.size() != 2)
			throw parseError(nameTok, "error_page expects 2 arguments: <code> <path>");
			
		int	code = parseIntStrict(args[0], nameTok);
		srv.errorPages[code] = args[1];
		return;
	}
	throw parseError(nameTok, "unknow directive in server: " + name);
}

void						ConfigParser::applyLocationDirective(const Tokenizer::Token &nameTok,
																const std::vector<std::string> &args,
																LocationConfig &loc)
{
	const std::string	&name = nameTok.text;
	if (name == "root")
	{
		if (args.size() != 1)
			throw parseError(nameTok, "root expects 1 argument");

		loc.hasRoot = true;
		loc.root = args[0];
		return;
	}
	if (name == "alias")
	{
		if (args.size() != 1)
			throw parseError(nameTok, "alias expects 1 argument");

		loc.hasAlias = true;
		loc.alias = args[0];
		return;
	}


	if (name == "index")
	{
		if (args.size() != 1)
			throw parseError(nameTok, "index expects 1 argument");

		loc.hasIndex = true;
		loc.index = args[0];
		return;
	}
	if (name == "client_max_body_size")
	{
		if (args.size() != 1)
			throw parseError(nameTok, "client_max_body_size expects 1 argument");

		loc.hasClientMaxBodySize = true;
		loc.clientMaxBodySize = parseSizeTStrict(args[0], nameTok);
		return;
	}
	if (name == "autoindex")
	{
		if (args.size() != 1)
			throw parseError(nameTok, "autoindex expects 1 argument: on|off");

		loc.hasAutoindex = true;
		if (args[0] == "on")
			loc.autoindex= true;
		else if (args[0] == "off")
			loc.autoindex= false;
		else
			throw parseError(nameTok, "autoindex expects 'on' or 'off'");
		return;
	}
	if (name == "allow_methods")
	{
		if (args.empty())
			throw parseError(nameTok, "allow_methods expects at least 1 method");

		loc.hasAllowedMethods = true;
		loc.allowedMethods = args;
		return;
	}
	if (name == "upload_dir")
	{
		if (args.size() != 1)
			throw parseError(nameTok, "upload_dir expects 1 argument");

		loc.hasUploadDir= true;
		loc.uploadDir= args[0];
		return;
	}
	if (name == "return")
	{
		if (args.size() != 2)
			throw parseError(nameTok, "return exppects 2 arguments: <code> <target>");

		loc.hasRedirect = true;
		loc.redirectCode = parseIntStrict(args[0], nameTok);
		loc.redirectTarget = args[1];
		return;
	}
	if (name == "cgi")
	{
		if (args.size() != 2)
			throw parseError(nameTok, "cgi expects 2 arguments: <ext> <executable>");

		const std::string	&ext = args[0];
		const std::string	&exe = args[1];

		if (ext.empty() || ext[0] != '.')
			throw parseError(nameTok, "cgi extension must start with '.'");

		loc.hasCgi = true;
		loc.cgiHandlers[ext] = exe;
		return;
	}
	throw parseError(nameTok, "unknow directive in location: " + name);
}

