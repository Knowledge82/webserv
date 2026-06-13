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

#include "Config.hpp"
#include "ConfigTokenizer.hpp"
#include <string>
#include <vector>

class	ConfigParser
{
public:
	explicit ConfigParser(const std::string &path);

	Config	parseConfig();

private:
	Tokenizer			tokenizer_;
	Tokenizer::Token	nextToken_;

	void				consumeToken();
	void				expect(Tokenizer::TokenType t, const char *description);
	bool				isWord(const char *w) const;

	// Grammar methods
	ServerConfig		parseServer();
	LocationConfig		parseLocation();

	// Parse directives
	void						parseServerDirective(ServerConfig &srv);
	void						parseLocationDirective(LocationConfig &loc);
	std::vector<std::string>	readArgsUntilSemi();// ";" == semicolon->Semi

	void						applyServerDirective(const Tokenizer::Token &nameTok,
									const std::vector<std::string> &args,
									ServerConfig &srv);
	void						applyLocationDirective(const Tokenizer::Token &nameTok,
									const std::vector<std::string> &args,
									LocationConfig &loc);
};

#endif
