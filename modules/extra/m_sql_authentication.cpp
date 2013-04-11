#include "module.h"
#include "sql.h"

static Module *me;

class SQLAuthenticationResult : public SQL::Interface
{
	Reference<User> user;
	IdentifyRequest *req;

 public:
	SQLAuthenticationResult(User *u, IdentifyRequest *r) : SQL::Interface(me), user(u), req(r)
	{
		req->Hold(me);
	}

	~SQLAuthenticationResult()
	{
		req->Release(me);
	}

	void OnResult(const SQL::Result &r) anope_override
	{
		if (r.Rows() == 0)
		{
			Log(LOG_DEBUG) << "m_sql_authentication: Unsuccessful authentication for " << req->GetAccount();
			delete this;
			return;
		}

		Log(LOG_DEBUG) << "m_sql_authentication: Successful authentication for " << req->GetAccount();

		Anope::string email;
		try
		{
			email = r.Get(0, "email");
		}
		catch (const SQL::Exception &) { }

		NickAlias *na = NickAlias::Find(req->GetAccount());
		if (na == NULL)
		{
			na = new NickAlias(req->GetAccount(), new NickCore(req->GetAccount()));
			if (user)
			{
				if (Config->NSAddAccessOnReg)
					na->nc->AddAccess(user->Mask());
		
				if (NickServ)
					user->SendMessage(NickServ, _("Your account \002%s\002 has been successfully created."), na->nick.c_str());
			}
		}

		if (!email.empty() && email != na->nc->email)
		{
			na->nc->email = email;
			if (user && NickServ)
				user->SendMessage(NickServ, _("Your email has been updated to \002%s\002."), email.c_str());
		}

		req->Success(me);
		delete this;
	}

	void OnError(const SQL::Result &r) anope_override
	{
		Log(this->owner) << "m_sql_authentication: Error executing query " << r.GetQuery().query << ": " << r.GetError();
		delete this;
	}
};

class ModuleSQLAuthentication : public Module
{
	Anope::string engine;
	Anope::string query;
	Anope::string disable_reason;

	ServiceReference<SQL::Provider> SQL;

 public:
	ModuleSQLAuthentication(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, EXTRA | VENDOR)
	{

		me = this;

		Implementation i[] = { I_OnReload, I_OnPreCommand, I_OnCheckAuthentication };
		ModuleManager::Attach(i, this, sizeof(i) / sizeof(Implementation));
	}

	void OnReload(ServerConfig *conf, ConfigReader &reader) anope_override
	{
		this->engine = reader.ReadValue("m_sql_authentication", "engine", "", 0);
		this->query = reader.ReadValue("m_sql_authentication", "query", "", 0);
		this->disable_reason = reader.ReadValue("m_sql_authentication", "disable_reason", "", 0);

		this->SQL = ServiceReference<SQL::Provider>("SQL::Provider", this->engine);
	}

	EventReturn OnPreCommand(CommandSource &source, Command *command, std::vector<Anope::string> &params) anope_override
	{
		if (!this->disable_reason.empty() && command->name == "nickserv/register")
		{
			source.Reply(this->disable_reason);
			return EVENT_STOP;
		}

		return EVENT_CONTINUE;
	}

	void OnCheckAuthentication(User *u, IdentifyRequest *req) anope_override
	{
		if (!this->SQL)
		{
			Log(this) << "Unable to find SQL engine";
			return;
		}

		SQL::Query q(this->query);
		q.SetValue("a", req->GetAccount());
		q.SetValue("p", req->GetPassword());
		if (u)
		{
			q.SetValue("n", u->nick);
			q.SetValue("i", u->ip);
		}
		else
		{
			q.SetValue("n", "");
			q.SetValue("i", "");
		}


		this->SQL->Run(new SQLAuthenticationResult(u, req), q);

		Log(LOG_DEBUG) << "m_sql_authentication: Checking authentication for " << req->GetAccount();
	}
};

MODULE_INIT(ModuleSQLAuthentication)
