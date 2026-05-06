/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigTokenizer.cpp                                :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/06 11:57:58 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/06 17:16:11 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ConfigTokenizer.hpp"
#include <stdexcept>

Tokenizer::Tokenizer(const std::string &path)
	: file_(path.c_str())
	, line_(1)
	, col_(0)
	, current_(0)
{
	if (!file_.is_open())
		throw std::runtime_error("cannot open config file: " + path);
	advance(); // таким образом current_ после конструктора = первый символ файла
}

void	Tokenizer::advance()
{
	current_ = file_.get();//.get() без аргументов читает один символ из потока std::ifstream и возвращает его как int. Конкретно тут читаем след символ, двигаем курсор потока.
	if (current_ == '\n')// если это перевод строки, то:
	{
		line_++; // увеличиваем номер строки
		col_ = 0;// сбрасываем колонку
	}
	else
		col_++; // иначе просто двигаем колонку вправо
} // и всё это, чтобы можно было сказать: "синтаксическая ошибка на строке 12, колонка 5". пиздато

Token	Tokenizer::makeToken(TokenType t, const std::string &txt, int line, int col)
{
	Token	tok;
	tok.type = t;
	tok.text = txt;
	tok.line = line;
	tok.col = col;
	
	return tok;
}

void	Tokenizer::skipSpacesAndComments()
{
	while (current_ != EOF)
	{
		if (current_ == ' ' || current_ == '\t' || current_ == '\r' || current_ == '\n')
		{
			advance();
			continue;
		}
		if (current_ == '#') //если видит #, съедает всё до конца строки (комментарий)
		{
			while (current_ != EOF && current_ != '\n') // пропуск до конца строки
				advance();
		   continue;	
		}
		break;
	}
}

Tokenizer::Token	Tokenizer::readWord()//“Слово” — это последовательность символов до:
										 //пробела/перевода строки
										 //спецсимволов { } ;
										 //# (комментарий начинается, когда слово закончилось)
{
	int	startLine = line_;
	int	startCol = col_;

	std::string	s;
	while (current_ != EOF)
	{
		if (current_ == ' ' || current_ == '\t' || current_ == '\r' || current_ == '\n')
			break;
		if (current_ == '{' || current_ == '}' || current_ == ';' || current_ == '#')
			break;
		s.push_back(static_cast<char>(current_));
		advance();
	}

	return makeToken(T_WORD, s, startLine, startCol);
}

Tokenizer::Token	Tokenizer::next() //диспетчер
{
	skipSpacesAndComments();

	if (current_ == EOF)
		return makeToken(T_EOF, "", line_, col_);

	int	startLine = line_;
	int	startCol = col_;

	if (current_ == '{')
	{
		advance();
		return makeToken(T_LBRACE, "{", startLine, startCol);
	}
	if (current_ == '}')
	{
		advance();
		return makeToken(T_RBRACE, "{", startLine, startCol);
	}
	if (current_ == ';')
	{
		advance();
		return makeToken(T_SEMI, "{", startLine, startCol);
	}

	return readWord();
}



