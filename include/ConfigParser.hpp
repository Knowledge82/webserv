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

#include "Config.hpp" //парсер должен создавать структуры.
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
	Tokenizer::Token	nextToken_; //“Текущий токен” (тот, на который мы сейчас смотрим), lookahead, следующий непрочитанный токен

	void				consumeToken(); // двигает поток токенов. НЕ ПРОВЕРЯЕТ, просто двигает.
	void				expect(Tokenizer::TokenType t, const char *description);
	bool				isWord(const char *w) const;

	// Grammar methods
	ServerConfig		parseServer();
	LocationConfig		parseLocation();

	//Директива — это одна команда/настройка в конфиге, которая заканчивается ";"
	// Parse directives
	void						parseServerDirective(ServerConfig &srv);
	void						parseLocationDirective(LocationConfig &loc);
	std::vector<std::string>	readArgsUntilSemi();//стандартная терминология: ";" == semicolon->Semi

	void						applyServerDirective(const Tokenizer::Token &nameTok,
									const std::vector<std::string> &args,
									ServerConfig &srv);
	void						applyLocationDirective(const Tokenizer::Token &nameTok,
									const std::vector<std::string> &args,
									LocationConfig &loc);
};
/* nextToken_ ВСЕГДА = следующий токен, который ещё не обработан.

Что гарантирует:

expect() проверяет текущий nextToken_, затем делает consumeToken()
consumeToken() — единственный способ двигаться вперёд
любая функция, когда заканчивает работу, оставляет nextToken_ на правильном “следующем” месте */


#endif
