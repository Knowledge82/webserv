/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigTokenizer.cpp                                :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/06 11:57:58 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/08 13:11:46 by vdarsuye         ###   ########.fr       */
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
	advance(); // so current_ after constructor = first character of the file
}

void	Tokenizer::advance()
{
	current_ = file_.get();//.get() without args reads one char from std::ifstream and returns it as int. Here we read the next char, advancing the stream cursor.
	if (current_ == '\n')// if this is a newline:
	{
		line_++; // increment line number
		col_ = 0;// reset column
	}
	else
		col_++; // otherwise just advance column right
} // all this so we can say: "syntax error on line 12, column 5". nice

Tokenizer::Token	Tokenizer::makeToken(TokenType t, const std::string &txt, int line, int col)
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
		if (current_ == '#') //if it sees #, eat everything until end of line (comment)
		{
			while (current_ != EOF && current_ != '\n') // skip until end of line
				advance();
		   continue;	
		}
		break;
	}
}

Tokenizer::Token	Tokenizer::readWord()//"Word" is a sequence of characters until:
										 //whitespace/newline
										 //special chars { } ;
										 //# (comment starts after the word ends)
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

Tokenizer::Token	Tokenizer::next() //dispatcher
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
		return makeToken(T_RBRACE, "}", startLine, startCol);
	}
	if (current_ == ';')
	{
		advance();
		return makeToken(T_SEMI, ";", startLine, startCol);
	}

	return readWord();
}



