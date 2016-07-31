/*
 * Anope IRC Services
 *
 * Copyright (C) 2003-2016 Anope Team <team@anope.org>
 *
 * This file is part of Anope. Anope is free software; you can
 * redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see see <http://www.gnu.org/licenses/>.
 */

#include "services.h"
#include "modules.h"
#include "protocol.h"
#include "servers.h"
#include "users.h"
#include "event.h"

void Anope::Process(const Anope::string &buffer)
{
	/* If debugging, log the buffer */
	Log(LOG_RAWIO) << "Received: " << buffer;

	if (buffer.empty())
		return;

	Anope::string source, command;
	std::vector<Anope::string> params;

	IRCD->Parse(buffer, source, command, params);

	if (Anope::ProtocolDebug)
	{
		Log() << "Source : " << (source.empty() ? "No source" : source);
		Log() << "Command: " << command;

		if (params.empty())
			Log() << "No params";
		else
			for (unsigned i = 0; i < params.size(); ++i)
				Log() << "params " << i << ": " << params[i];
	}

	if (command.empty())
	{
		Log(LOG_DEBUG) << "No command? " << buffer;
		return;
	}

	static const Anope::string proto_name = ModuleManager::FindFirstOf(PROTOCOL) ? ModuleManager::FindFirstOf(PROTOCOL)->name : "";

	MessageSource src(source);

	EventReturn MOD_RESULT = EventManager::Get()->Dispatch(&Event::Message::OnMessage, src, command, params);

	ServiceReference<IRCDMessage> m(proto_name + "/" + command.lower());
	if (!m)
	{
		Log(LOG_DEBUG) << "unknown message from server (" << buffer << ")";
		return;
	}

	if (m->HasFlag(IRCDMESSAGE_SOFT_LIMIT) ? (params.size() < m->GetParamCount()) : (params.size() != m->GetParamCount()))
		Log(LOG_DEBUG) << "invalid parameters for " << command << ": " << params.size() << " != " << m->GetParamCount();
	else if (m->HasFlag(IRCDMESSAGE_REQUIRE_USER) && !src.GetUser())
		Log(LOG_DEBUG) << "unexpected non-user source " << source << " for " << command;
	else if (m->HasFlag(IRCDMESSAGE_REQUIRE_SERVER) && !source.empty() && !src.GetServer())
		Log(LOG_DEBUG) << "unexpected non-server source " << source << " for " << command;
	else
		m->Run(src, params);
}

void IRCDProto::Parse(const Anope::string &buffer, Anope::string &source, Anope::string &command, std::vector<Anope::string> &params)
{
	spacesepstream sep(buffer);

	if (buffer[0] == ':')
	{
		sep.GetToken(source);
		source.erase(0, 1);
	}

	sep.GetToken(command);
	
	for (Anope::string token; sep.GetToken(token);)
	{
		if (token[0] == ':')
		{
			if (!sep.StreamEnd())
				params.push_back(token.substr(1) + " " + sep.GetRemaining());
			else
				params.push_back(token.substr(1));
			break;
		}
		else
			params.push_back(token);
	}
}

Anope::string IRCDProto::Format(const Anope::string &source, const Anope::string &message)
{
	if (!source.empty())
		return ":" + source + " " + message;
	else
		return message;
}

