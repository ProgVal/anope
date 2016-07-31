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
#include "modules/ldap.h"
#include "modules/nickserv.h"

static Module *me;

static Anope::string basedn;
static Anope::string search_filter;
static Anope::string object_class;
static Anope::string email_attribute;
static Anope::string username_attribute;

struct IdentifyInfo
{
	Reference<User> user;
	IdentifyRequest *req;
	ServiceReference<LDAPProvider> lprov;
	bool admin_bind;
	Anope::string dn;

	IdentifyInfo(User *u, IdentifyRequest *r, ServiceReference<LDAPProvider> &lp) : user(u), req(r), lprov(lp), admin_bind(true)
	{
		req->Hold(me);
	}

	~IdentifyInfo()
	{
		req->Release(me);
	}
};

class IdentifyInterface : public LDAPInterface
{
	IdentifyInfo *ii;

 public:
	IdentifyInterface(Module *m, IdentifyInfo *i) : LDAPInterface(m), ii(i) { }

	~IdentifyInterface()
	{
		delete ii;
	}

	void OnDelete() anope_override
	{
		delete this;
	}

	void OnResult(const LDAPResult &r) override
	{
		if (!ii->lprov)
			return;

		switch (r.type)
		{
			case QUERY_SEARCH:
			{
				if (!r.empty())
				{
					try
					{
						const LDAPAttributes &attr = r.get(0);
						ii->dn = attr.get("dn");
						Log(LOG_DEBUG) << "m_ldap_authenticationn: binding as " << ii->dn;

						ii->lprov->Bind(new IdentifyInterface(this->owner, ii), ii->dn, ii->req->GetPassword());
						ii = NULL;
					}
					catch (const LDAPException &ex)
					{
						Log(this->owner) << "Error binding after search: " << ex.GetReason();
					}
				}
				break;
			}
			case QUERY_BIND:
			{
				if (ii->admin_bind)
				{
					Anope::string sf = search_filter.replace_all_cs("%account", ii->req->GetAccount()).replace_all_cs("%object_class", object_class);
					try
					{
						Log(LOG_DEBUG) << "m_ldap_authentication: searching for " << sf;
						ii->lprov->Search(new IdentifyInterface(this->owner, ii), basedn, sf);
						ii->admin_bind = false;
						ii = NULL;
					}
					catch (const LDAPException &ex)
					{
						Log(this->owner) << "Unable to search for " << sf << ": " << ex.GetReason();
					}
				}
				else
				{
					NickServ::Nick *na = NickServ::FindNick(ii->req->GetAccount());
					if (na == NULL)
					{
						na = new NickServ::Nick(ii->req->GetAccount(), new NickServ::Account(ii->req->GetAccount()));
						na->SetLastRealname(ii->user ? ii->user->realname : ii->req->GetAccount());
						NickServ::EventManager::Get()->Dispatch(&NickServ::Event::NickRegister::OnNickRegister, ii->user, na, ii->req->GetPassword());;
						ServiceBot *NickServ = Config->GetClient("NickServ");
						if (ii->user && NickServ)
							ii->user->SendMessage(NickServ, _("Your account \002%s\002 has been successfully created."), na->GetNick().c_str());
					}
					// encrypt and store the password in the nickcore
					Anope::Encrypt(ii->req->GetPassword(), na->GetAccount()->pass);

					na->GetAccount()->Extend<Anope::string>("m_ldap_authentication_dn", ii->dn);
					ii->req->Success(me);
				}
				break;
			}
			default:
				break;
		}
	}

	void OnError(const LDAPResult &r) override
	{
	}
};

class OnIdentifyInterface : public LDAPInterface
{
	Anope::string uid;

 public:
	OnIdentifyInterface(Module *m, const Anope::string &i) : LDAPInterface(m), uid(i) { }

	void OnDelete() anope_override
	{
		delete this;
	}

	void OnResult(const LDAPResult &r) override
	{
		User *u = User::Find(uid);

		if (!u || !u->Account() || r.empty())
			return;

		try
		{
			const LDAPAttributes &attr = r.get(0);
			Anope::string email = attr.get(email_attribute);

			if (!email.equals_ci(u->Account()->GetEmail()))
			{
				u->Account()->GetEmail() = email;
				ServiceBot *NickServ = Config->GetClient("NickServ");
				if (NickServ)
					u->SendMessage(NickServ, _("Your email has been updated to \002%s\002"), email.c_str());
				Log(this->owner) << "Updated email address for " << u->nick << " (" << u->Account()->GetDisplay() << ") to " << email;
			}
		}
		catch (const LDAPException &ex)
		{
			Log(this->owner) << ex.GetReason();
		}
	}

	void OnError(const LDAPResult &r) override
	{
		Log(this->owner) << r.error;
	}
};

class OnRegisterInterface : public LDAPInterface
{
 public:
	OnRegisterInterface(Module *m) : LDAPInterface(m) { }

	void OnResult(const LDAPResult &r) override
	{
		Log(this->owner) << "Successfully added newly created account to LDAP";
	}

	void OnError(const LDAPResult &r) override
	{
		Log(this->owner) << "Error adding newly created account to LDAP: " << r.getError();
	}
};

class ModuleLDAPAuthentication : public Module
	, public EventHook<Event::PreCommand>
	, public EventHook<Event::CheckAuthentication>
	, public EventHook<Event::NickIdentify>
	, public EventHook<NickServ::Event::NickRegister>
{
	ServiceReference<LDAPProvider> ldap;
	OnRegisterInterface orinterface;

	PrimitiveExtensibleItem<Anope::string> dn;

	Anope::string password_attribute;
	Anope::string disable_register_reason;
	Anope::string disable_email_reason;

 public:
	ModuleLDAPAuthentication(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, EXTRA | VENDOR)
		, EventHook<Event::PreCommand>("OnPreCommand", EventHook<Event::PreCommand>::Priority::FIRST)
		, EventHook<Event::CheckAuthentication>("OnCheckAuthentication", EventHook<Event::CheckAuthentication>::Priority::FIRST)
		, EventHook<Event::NickIdentify>("OnNickIdentify", EventHook<Event::NickIdentify>::Priority::FIRST)
		, EventHook<NickServ::Event::NickRegister>("OnNickRegister", EventHook<NickServ::Event::NickRegister>::Priority::FIRST)
		, ldap("LDAPProvider", "ldap/main")
		, orinterface(this)
		, dn(this, "m_ldap_authentication_dn")
	{
		me = this;
	}

	void OnReload(Configuration::Conf *config) override
	{
		Configuration::Block *conf = Config->GetModule(this);

		basedn = conf->Get<Anope::string>("basedn");
		search_filter = conf->Get<Anope::string>("search_filter");
		object_class = conf->Get<Anope::string>("object_class");
		username_attribute = conf->Get<Anope::string>("username_attribute");
		this->password_attribute = conf->Get<Anope::string>("password_attribute");
		email_attribute = conf->Get<Anope::string>("email_attribute");
		this->disable_register_reason = conf->Get<Anope::string>("disable_register_reason");
		this->disable_email_reason = conf->Get<Anope::string>("disable_email_reason");

		if (!email_attribute.empty())
			/* Don't complain to users about how they need to update their email, we will do it for them */
			config->GetModule("nickserv")->Set("forceemail", "false");
	}

	EventReturn OnPreCommand(CommandSource &source, Command *command, std::vector<Anope::string> &params) override
	{
		if (!this->disable_register_reason.empty())
		{
			if (command->name == "nickserv/register" || command->name == "nickserv/group")
			{
				source.Reply(this->disable_register_reason);
				return EVENT_STOP;
			}
		}

		if (!email_attribute.empty() && !this->disable_email_reason.empty() && command->name == "nickserv/set/email")
		{
			source.Reply(this->disable_email_reason);
			return EVENT_STOP;
		}

		return EVENT_CONTINUE;
	}

	void OnCheckAuthentication(User *u, IdentifyRequest *req) override
	{
		if (!this->ldap)
			return;

		IdentifyInfo *ii = new IdentifyInfo(u, req, this->ldap);
		this->ldap->BindAsAdmin(new IdentifyInterface(this, ii));
	}

	void OnNickIdentify(User *u) override
	{
		if (email_attribute.empty() || !this->ldap)
			return;

		Anope::string *d = dn.Get(u->Account());
		if (!d || d->empty())
			return;

		this->ldap->Search(new OnIdentifyInterface(this, u->GetUID()), *d, "(" + email_attribute + "=*)");
	}

	void OnNickRegister(User *, NickServ::Nick *na, const Anope::string &pass) override
	{
		if (!this->disable_register_reason.empty() || !this->ldap)
			return;

		this->ldap->BindAsAdmin(NULL);

		LDAPMods attributes;
		attributes.resize(4);

		attributes[0].name = "objectClass";
		attributes[0].values.push_back("top");
		attributes[0].values.push_back(object_class);

		attributes[1].name = username_attribute;
		attributes[1].values.push_back(na->GetNick());

		if (!na->GetAccount()->GetEmail().empty())
		{
			attributes[2].name = email_attribute;
			attributes[2].values.push_back(na->GetAccount()->GetEmail());
		}

		attributes[3].name = this->password_attribute;
		attributes[3].values.push_back(pass);

		Anope::string new_dn = username_attribute + "=" + na->GetNick() + "," + basedn;
		this->ldap->Add(&this->orinterface, new_dn, attributes);
	}
};

MODULE_INIT(ModuleLDAPAuthentication)
