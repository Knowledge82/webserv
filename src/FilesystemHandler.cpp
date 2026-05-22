/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   FilesystemHandler.cpp                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 18:45:43 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/22 16:26:35 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "FilesystemHandler.hpp"
#include "Filesystem.hpp"
#include "Autoindex.hpp"
#include "Path.hpp"
#include "Mime.hpp"

namespace
{
	Http::HttpReply	makeError(int status)
	{
		Http::HttpReply	r;
		r.kind = Http::REPLY_ERROR;
		r.status = status;

		return r;
	}

	Http::HttpReply makeRedirect(int code, const std::string &target)
	{
		Http::HttpReply r;
		r.kind = Http::REPLY_REDIRECT;
		r.redirectCode = code;
		r.location = target;
		return r;
	}

	Http::HttpReply makeOk(const std::string &type, const std::string &body)
	{
		Http::HttpReply r;
		r.kind = Http::REPLY_NORMAL;
		r.status = 200;
		r.contentType = type;
		r.body = body;
		return r;
	}
}


namespace Http
{
	HttpReply buildFileSystemReply(const EffectiveConfig &eff,
	                               const LocationConfig *loc,
	                               const std::string &uri)
	{
		std::string	path;

		// (A) Special-case "/" -> root/index
		if (uri == "/")
		{
			if (!eff.hasIndex)
				return makeError(403);
			
			path = Fs::joinPath(eff.root, eff.index);

			Fs::PathKind	pk = Fs::classifyPath(path);
			if (pk != Fs::PATH_FILE)
			{
				if (pk == Fs::PATH_DIR)
					return makeError(403);
				return makeError(Fs::pathKindToHttpStatus(pk));
			}
		
			std::string	body;
			if (!Fs::readFileToString(path, body))
				return makeError(500);
			return makeOk(Http::guessContentType(path), body);
		}

		// (B) alias sanity (must have loc if alias is used)
		if (eff.hasAlias && !loc)
			return makeError(500);
	
		// (C) map URI -> filesystem path using safeJoin/safeJoinAlias
		{
			int	safeStatus = 200;

			if (eff.hasAlias)
			{
				// loc is non-null here because of check above
				if (!Http::safeJoinAlias(eff.alias, prefix, uri, path, safeStatus))
					return makeError(safeStatus);
			}
			else
			{
				if (!Http::safeJoin(eff.root, uri, path, safeStatus))
					return makeError(safeStatus);
			}
		}

		// (D) stat/classify
		Fs::PathKind	pk = Fs::classifyPath(path);

		// If stat says missing/forbidden/error: answer immediately
		if (pk == Fs::PATH_MISSING || pk == Fs::PATH_FORBIDDEN || pk == Fs::PATH_ERROR)
			return makeError(Fs::pathKindToHttStatus(pk));

		// (E) directory flow
		if (pk == Fs::PATH_DIR)
		{
			// Redirect "/dir" -> "/dir/" to keep relative links correct
			if (!Http::endsWithSlash(uri))
				return makeRedirect(301, uri + "/");
	
			// index handling
			if (eff.hasIndex)
			{
				std::string		indexPath = Fs::joinPath(path, eff.index);
				Fs::PathKind	ik = Fs::classifyPath(indexPath);

				if (ik == Fs::PATH_MISSING)
					return makeError(404);	
				if (ik == Fs::PATH_FILE)
				{
					std::string	body;
					if (!Fs::readFileToString(indexPath, body))
						return makeError(500);
					return makeOk(Http::guessContentType(indexPath), body);
				}
				if (ik == Fs::PATH_FORBIDDEN)
					return makeError(403);
				if (ik == Fs::PATH_ERROR)
					return makeError(500);
				if (ik == Fs::PATH_DIR)
					return makeError(404); //tester-friendly, keep behavior
			}

			// autoindex
			if (eff.hasAutoindex && eff.autoindex)
			{
				std::string listing;
				
				if (!Http::appendDirectoryListingHtml(listing, uri, path))
					return makeError(403);
				return makeOk("text/html", listing);	
			}

			return makeError(403);
		}

		// (F) File flow
		{
		std::string	body;
		if (!Fs::readFileToString(path, body))
			return makeError(500);
		
		return makeOk(Http::guessContentType(path), body);
		}
	}
}
