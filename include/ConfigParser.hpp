/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigParser.hpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/06 17:16:35 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/08 13:22:12 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include "Config.hpp" //parser creates config structures
#include "ConfigTokenizer.hpp"//parser consumes tokens
#include <string>
#include <vector>

class	ConfigParser
{
public:
	explicit ConfigParser(const std::string &path);

	Config	parseConfig();//turns config file into Config struct
	//Contract:
	//on success returns populated Config
	//on error throws std::runtime_error with line/col

private:
	Tokenizer			tokenizer_; //tokenizer object: reads file and yields tokens on demand
	Tokenizer::Token	nextToken_; //"current token" (the one we're looking at), lookahead, next unread token

	void				consumeToken(); // advances token stream. Does NOT check, just advances.
	void				expect(Tokenizer::TokenType t, const char *description);
	bool				isWord(const char *w) const;

	// Grammar methods
	ServerConfig		parseServer();
	LocationConfig		parseLocation();

	//Directive — one config command/setting ending with ";"
	// Parse directives
	void						parseServerDirective(ServerConfig &srv);
	void						parseLocationDirective(LocationConfig &loc);
	std::vector<std::string>	readArgsUntilSemi();//standard terminology: ";" == semicolon

	void						applyServerDirective(const Tokenizer::Token &nameTok,
									const std::vector<std::string> &args,
									ServerConfig &srv);
	void						applyLocationDirective(const Tokenizer::Token &nameTok,
									const std::vector<std::string> &args,
									LocationConfig &loc);
};
/* nextToken_ is ALWAYS the next unprocessed token.

Guarantees:

expect() checks current nextToken_, then calls consumeToken()
consumeToken() is the only way to advance
any function, when done, leaves nextToken_ at the correct "next" position */


#endif
