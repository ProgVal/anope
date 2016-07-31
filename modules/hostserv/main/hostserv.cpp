/*
 * Anope IRC Services
 *
 * Copyright (C) 2011-2016 Anope Team <team@anope.org>
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

#include "module.h"
#include "modules/nickserv/update.h"
#include "modules/hostserv/del.h"
#include "modules/help.h"

class HostServCore : public Module
	, public EventHook<Event::UserLogin>
	, public EventHook<Event::NickUpdate>
	, public EventHook<Event::Help>
	, public EventHook<Event::SetVhost>
	, public EventHook<Event::DeleteVhost>
{
	Reference<ServiceBot> HostServ;

 public:
	HostServCore(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, PSEUDOCLIENT | VENDOR)
		, EventHook<Event::UserLogin>(this)
		, EventHook<Event::NickUpdate>(this)
		, EventHook<Event::Help>(this)
		, EventHook<Event::SetVhost>(this)
		, EventHook<Event::DeleteVhost>(this)
	{
		if (!IRCD || !IRCD->CanSetVHost)
			throw ModuleException("Your IRCd does not support vhosts");
	}

	void OnReload(Configuration::Conf *conf) override
	{
		const Anope::string &hsnick = conf->GetModule(this)->Get<Anope::string>("client");

		if (hsnick.empty())
			throw ConfigException(Module::name + ": <client> must be defined");

		ServiceBot *bi = ServiceBot::Find(hsnick, true);
		if (!bi)
			throw ConfigException(Module::name + ": no bot named " + hsnick);

		HostServ = bi;
	}

	void OnUserLogin(User *u) override
	{
		if (!IRCD->CanSetVHost)
			return;

		NickServ::Nick *na = NickServ::FindNick(u->nick);
		if (!na || na->GetAccount() != u->Account() || !na->HasVhost())
			na = NickServ::FindNick(u->Account()->GetDisplay());
		if (!na || !na->HasVhost())
			return;

		if (u->vhost.empty() || !u->vhost.equals_cs(na->GetVhostHost()) || (!na->GetVhostIdent().empty() && !u->GetVIdent().equals_cs(na->GetVhostIdent())))
		{
			IRCD->SendVhost(u, na->GetVhostIdent(), na->GetVhostHost());

			u->vhost = na->GetVhostHost();
			u->UpdateHost();

			if (IRCD->CanSetVIdent && !na->GetVhostIdent().empty())
				u->SetVIdent(na->GetVhostIdent());

			if (HostServ)
			{
				if (!na->GetVhostIdent().empty())
					u->SendMessage(*HostServ, _("Your vhost of \002%s\002@\002%s\002 is now activated."), na->GetVhostIdent().c_str(), na->GetVhostHost().c_str());
				else
					u->SendMessage(*HostServ, _("Your vhost of \002%s\002 is now activated."), na->GetVhostHost().c_str());
			}
		}
	}

	void OnNickUpdate(User *u) override
	{
		this->OnUserLogin(u);
	}

	EventReturn OnPreHelp(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!params.empty() || source.c || source.service != *HostServ)
			return EVENT_CONTINUE;
		source.Reply(_("%s commands:"), HostServ->nick.c_str());
		return EVENT_CONTINUE;
	}

	void OnPostHelp(CommandSource &source, const std::vector<Anope::string> &params) override
	{
	}

	void OnSetVhost(NickServ::Nick *na) override
	{
		if (Config->GetModule(this)->Get<bool>("activate_on_set"))
		{
			User *u = User::Find(na->GetNick());

			if (u && u->Account() == na->GetAccount())
			{
				IRCD->SendVhost(u, na->GetVhostIdent(), na->GetVhostHost());

				u->vhost = na->GetVhostHost();
				u->UpdateHost();

				if (IRCD->CanSetVIdent && !na->GetVhostIdent().empty())
					u->SetVIdent(na->GetVhostIdent());

				if (HostServ)
				{
					if (!na->GetVhostIdent().empty())
						u->SendMessage(*HostServ, _("Your vhost of \002%s\002@\002%s\002 is now activated."), na->GetVhostIdent().c_str(), na->GetVhostHost().c_str());
					else
						u->SendMessage(*HostServ, _("Your vhost of \002%s\002 is now activated."), na->GetVhostHost().c_str());
				}
			}
		}
	}

	void OnDeleteVhost(NickServ::Nick *na) override
	{
		if (Config->GetModule(this)->Get<bool>("activate_on_set"))
		{
			User *u = User::Find(na->GetNick());

			if (u && u->Account() == na->GetAccount())
				IRCD->SendVhostDel(u);
		}
	}
};

MODULE_INIT(HostServCore)

