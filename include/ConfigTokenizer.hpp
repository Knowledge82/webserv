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
	enum	TokenType //minimal set for nginx-like syntax without quotes
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
	std::ifstream	file_;		// the character source
	int				line_;		// current
	int				col_;		// position
	int				current_; // current char or EOF. int because ifstream::get() returns int,
							  // which can be the special EOF marker.
							  // If stored in char, we can't distinguish a real byte from EOF

	void			advance();					// next symbol
	void			skipSpacesAndComments();	// skip garbage
	Token			makeToken(TokenType t, const std::string &txt, int line, int col);
	Token			readWord();					// read T_WORD

};
/*
	What the tokenizer does NOT do:

	1. No quotes. root "/tmp/my site"; won't parse. Intentional design choice.

	2. No escape sequences. \; in an argument — not supported.

	3. No # inside words. foo#bar becomes token foo then comment bar. Same behavior as nginx.

	4. No word content validation. Doesn't know what "port" or "method" is — that's the parser/semantic layer's job.
*/
#endif
