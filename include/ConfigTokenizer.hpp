/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigTokenizer.hpp                                :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/06 11:49:22 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/12 12:02:27 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIGTOKENIZER_HPP
#define CONFIGTOKENIZER_HPP

#include <string>
#include <fstream>

class	Tokenizer
{
public:
	enum	TokenType //минимальный набор для nginx-like синтаксиса без кавычек
	{
		T_WORD,
		T_LBRACE,
		T_RBRACE,
		T_SEMI,
		T_EOF		
	};

	struct	Token
	{
		TokenType	type;
		std::string	text;
		int			line;
		int			col;
	};

	explicit Tokenizer(const std::string &path);

	Token	next();

private:
	std::ifstream	file_;		// сам источник символов
	int				line_;		// текущее
	int				col_;		// положение
	int				current_; // current char or EOF. в int  потому что ifstream::get() возвращает int
							  // где возможен специальный маркер EOF. 
							  // Если хранить в char,потеряем возможность отличить реальный байт от EOF

	void			advance();					// next symbol
	void			skipSpacesAndComments();	// skip garbage
	Token			makeToken(TokenType t, const std::string &txt, int line, int col);
	Token			readWord();					// read T_WORD

};
/*
	Что токенайзер НЕ делает:

	1. Нет кавычек. root "/tmp/my site"; не распарсится. Мы решили так сознательно.

	2. Нет escape-последовательностей. Типа \; в аргументе — не умеем и не надо.

	3. Нет поддержки # внутри слова. foo#bar будет токеном foo и потом комментарий bar. В nginx это тоже обычно комментарий. Для нас ок.

	4. Не валидирует содержимое слова. Он не знает, что такое “порт” или “метод”. Это делает парсер/семантика.
*/
#endif
