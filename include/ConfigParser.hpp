/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigParser.hpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/06 17:16:35 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/06 17:59:04 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include "Config.hpp" //парсер должне создавать структуры.
#include "ConfigTokenizer.hpp"// парсер потребляет токены.
#include <string>
#include <vector>

class	ConfigParser
{
public:
	explicit ConfigParser(const std::string &path);

	Config	parseConfig();// превращает файл конфига в Config.
	//Контракт:
	//на успехе возвращает заполненный Config
	//на ошибке кидает std::runtime_error с line/col

private:
	Tokenizer			tokenizer_; //Объект токенайзера: читает файл и выдаёт токены по запросу.
	Tokenizer::Token	la_; //“Текущий токен” (тот, на который мы сейчас смотрим).

	void				consumeToken(); // двигает поток токенов. НЕ ПРОВЕРЯЕТ, просто двигает.
	void				expect(Tokenizer::TokenType t, const char *description);
	bool				isWord(const char *w) const;

	ServerConfig		parseServer();
	LocationConfig		parseLocation();

	//Директива — это одна команда/настройка в конфиге, которая заканчивается ";"
	void				parseServerDirective(ServerConfig &srv);
	void				parseLocationDirective(LocationConfig &loc);

	std::vector<std::string>	readArgsUntilSemi();

	void				parseDirectiveCommon(const Tokenizer::Token &nameTok,
							const std::vector<std::string> &args,
							ServerConfig &srv,
							LocationConfig *loc);
};

#endif
