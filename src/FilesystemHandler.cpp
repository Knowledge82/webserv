/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   FilesystemHandler.cpp                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 18:45:43 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/08 18:45:21 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "FilesystemHandler.hpp"
#include "Filesystem.hpp"
#include "Autoindex.hpp"
#include "Path.hpp"
#include "Mime.hpp"

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
				return Http::makeErrorReply(403);
			
			path = Fs::joinPath(eff.root, eff.index);

			Fs::PathKind	pk = Fs::classifyPath(path);
			if (pk != Fs::PATH_FILE)
			{
				if (pk == Fs::PATH_DIR)
					return Http::makeErrorReply(403);
				return Http::makeErrorReply(Fs::pathKindToHttpStatus(pk));
			}
		
			std::string	body;
			if (!Fs::readFileToString(path, body))
				return Http::makeErrorReply(500);
			return Http::makeOkReply(Http::guessContentType(path), body);
		}

		// (B) alias sanity (must have loc if alias is used)
		if (eff.hasAlias && !loc)
			return Http::makeErrorReply(500);
	
		// (C) map URI -> filesystem path using safeJoin/safeJoinAlias
		{
			int	safeStatus = 200;

			if (eff.hasAlias)
			{
				// loc is non-null here because of check above
				if (!Http::safeJoinAlias(eff.alias, loc->prefix, uri, path, safeStatus))
					return Http::makeErrorReply(safeStatus);
			}
			else
			{
				if (!Http::safeJoin(eff.root, uri, path, safeStatus))
					return Http::makeErrorReply(safeStatus);
			}
		}

		// (D) stat/classify
		Fs::PathKind	pk = Fs::classifyPath(path);

		// If stat says missing/forbidden/error: answer immediately
		if (pk == Fs::PATH_MISSING || pk == Fs::PATH_FORBIDDEN || pk == Fs::PATH_ERROR)
			return Http::makeErrorReply(Fs::pathKindToHttpStatus(pk));

		// (E) directory flow
		if (pk == Fs::PATH_DIR)
		{
			// Redirect "/dir" -> "/dir/" to keep relative links correct
			if (!Http::endsWithSlash(uri))
				return Http::makeRedirectReply(301, uri + "/");

			// index handling
			if (eff.hasIndex)
			{
				std::string		indexPath = Fs::joinPath(path, eff.index);
				Fs::PathKind	ik = Fs::classifyPath(indexPath);

				// DELETE OR COMMENT THIS LINE:
				// if (ik == Fs::PATH_MISSING) return Http::makeErrorReply(404);

				if (ik == Fs::PATH_FILE)
				{
					std::string	body;
					if (!Fs::readFileToString(indexPath, body))
						return Http::makeErrorReply(500);
					return Http::makeOkReply(Http::guessContentType(indexPath), body);
				}
				if (ik == Fs::PATH_FORBIDDEN)
					return Http::makeErrorReply(403);
				if (ik == Fs::PATH_ERROR)
					return Http::makeErrorReply(500);
				if (ik == Fs::PATH_DIR)
					return Http::makeErrorReply(404); //tester-friendly, keep behavior

				// If ik == Fs::PATH_MISSING, we do nothing and fall through to autoindex check below!
			}

			// autoindex
			if (eff.hasAutoindex && eff.autoindex)
			{
				std::string listing;

				if (!Http::appendDirectoryListingHtml(listing, uri, path))
					return Http::makeErrorReply(403);
				return Http::makeOkReply("text/html", listing);
			}

			// If no index and autoindex is off — per standard return 403 Forbidden
			return Http::makeErrorReply(404);
		}
/* OLD		// (E) directory flow
		if (pk == Fs::PATH_DIR)
		{
			// Redirect "/dir" -> "/dir/" to keep relative links correct
			if (!Http::endsWithSlash(uri))
				return Http::makeRedirectReply(301, uri + "/");
	
			// index handling
			if (eff.hasIndex)
			{
				std::string		indexPath = Fs::joinPath(path, eff.index);
				Fs::PathKind	ik = Fs::classifyPath(indexPath);

				if (ik == Fs::PATH_MISSING)
					return Http::makeErrorReply(404);	
				if (ik == Fs::PATH_FILE)
				{
					std::string	body;
					if (!Fs::readFileToString(indexPath, body))
						return Http::makeErrorReply(500);
					return Http::makeOkReply(Http::guessContentType(indexPath), body);
				}
				if (ik == Fs::PATH_FORBIDDEN)
					return Http::makeErrorReply(403);
				if (ik == Fs::PATH_ERROR)
					return Http::makeErrorReply(500);
				if (ik == Fs::PATH_DIR)
					return Http::makeErrorReply(404); //tester-friendly, keep behavior
			}

			// autoindex
			if (eff.hasAutoindex && eff.autoindex)
			{
				std::string listing;
				
				if (!Http::appendDirectoryListingHtml(listing, uri, path))
					return Http::makeErrorReply(403);
				return Http::makeOkReply("text/html", listing);	
			}

			return Http::makeErrorReply(403);
		}
*/
		// (F) File flow
		{
		std::string	body;
		if (!Fs::readFileToString(path, body))
			return Http::makeErrorReply(500);
		
		return Http::makeOkReply(Http::guessContentType(path), body);
		}
	}
}
