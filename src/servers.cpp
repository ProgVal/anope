/* Routines to maintain a list of connected servers
 *
 * (C) 2003-2011 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church.
 */

#include "services.h"
#include "modules.h"

/* Anope */
Server *Me = NULL;

CapabInfo Capab_Info[] = {
	{"NOQUIT", CAPAB_NOQUIT},
	{"TSMODE", CAPAB_TSMODE},
	{"UNCONNECT", CAPAB_UNCONNECT},
	{"QS", CAPAB_QS},
	{"", CAPAB_END}
};

static const Anope::string CapabFlags = "";
Flags<CapabType, CAPAB_END> Capab(&CapabFlags);

/** Constructor
 * @param uplink The uplink this server is from, is only NULL when creating Me
 * @param name The server name
 * @param hops Hops from services server
 * @param description Server rdescription
 * @param sid Server sid/numeric
 * @param flag An optional server flag
 */
Server::Server(Server *uplink, const Anope::string &name, unsigned hops, const Anope::string &description, const Anope::string &sid, ServerFlag flag) : Flags<ServerFlag>(ServerFlagStrings), Name(name), Hops(hops), Description(description), SID(sid), UplinkServer(uplink)
{
	this->SetFlag(SERVER_SYNCING);
	this->SetFlag(flag);

	Log(this, "connect") << "uplinked to " << (this->UplinkServer ? this->UplinkServer->GetName() : "no uplink") << " connected to the network";

	/* Add this server to our uplinks leaf list */
	if (this->UplinkServer)
	{
		this->UplinkServer->AddLink(this);

		/* Check to be sure the uplink server only has one uplink, otherwise we introduce our clients if we jupe servers */
		if (Me == this->UplinkServer && !this->HasFlag(SERVER_JUPED))
		{
			/* Bring in our pseudo-clients */
			introduce_user("");
		}
	}
}

/** Destructor
 */
Server::~Server()
{
	Log(this, "quit") << "quit from " << (this->UplinkServer ? this->UplinkServer->GetName() : "no uplink") << " for " << this->QReason;

	if (Capab.HasFlag(CAPAB_NOQUIT) || Capab.HasFlag(CAPAB_QS))
	{
		patricia_tree<User *, ci::ci_char_traits>::iterator uit(UserListByNick);
		for (bool next = uit.next(); next;)
		{
			User *u = *uit;
			next = uit.next();

			if (u->server == this)
			{
				NickAlias *na = findnick(u->nick);
				if (na && !na->HasFlag(NS_FORBIDDEN) && (!na->nc->HasFlag(NI_SUSPENDED)) && (u->IsRecognized() || u->IsIdentified()))
				{
					na->last_seen = Anope::CurTime;
					na->last_quit = this->QReason;
				}

				delete u;
			}
		}

		Log(LOG_DEBUG) << "Finished removing all users for " << this->GetName();
	}

	if (this->UplinkServer)
		this->UplinkServer->DelLink(this);

	for (unsigned i = this->Links.size(); i > 0; --i)
		delete this->Links[i - 1];
}

/** Delete this server with a reason
 * @param reason The reason
 */
void Server::Delete(const Anope::string &reason)
{
	this->QReason = reason;
	delete this;
}

/** Get the name for this server
 * @return The name
 */
const Anope::string &Server::GetName() const
{
	return this->Name;
}

/** Get the number of hops this server is from services
 * @return Number of hops
 */
unsigned Server::GetHops() const
{
	return this->Hops;
}

/** Set the server description
 * @param desc The new description
 */
void Server::SetDescription(const Anope::string &desc)
{
	this->Description = desc;
}

/** Get the server description
 * @return The server description
 */
const Anope::string &Server::GetDescription() const
{
	return this->Description;
}

/** Get the server numeric/SID
 * @return The numeric/SID
 */
const Anope::string &Server::GetSID() const
{
	return this->SID;
}

/** Get the list of links this server has, or NULL if it has none
 * @return A list of servers
 */
const std::vector<Server *> &Server::GetLinks() const
{
	return this->Links;
}

/** Get the uplink server for this server, if this is our uplink will be Me
 * @return The servers uplink
 */
Server *Server::GetUplink()
{
	return this->UplinkServer;
}

/** Adds a link to this server
 * @param s The linking server
 */
void Server::AddLink(Server *s)
{
	this->Links.push_back(s);

	Log(this, "connect") << "introduced " << s->GetName();
}

/** Delinks a server from this server
 * @param s The server
 */
void Server::DelLink(Server *s)
{
	if (this->Links.empty())
		throw CoreException("Server::DelLink called on " + this->GetName() + " for " + s->GetName() + " but we have no links?");

	for (unsigned i = 0, j = this->Links.size(); i < j; ++i)
	{
		if (this->Links[i] == s)
		{
			this->Links.erase(this->Links.begin() + i);
			break;
		}
	}

	Log(this, "quit") << "quit " << s->GetName();
}

/** Remov all links from this server
 */
void Server::ClearLinks()
{
	for (unsigned i = 0, j = this->Links.size(); i < j; ++i)
		delete this->Links[i];
	this->Links.clear();
}

/** Finish syncing this server and optionally all links to it
 * @param SyncLinks True to sync the links for this server too (if any)
 */
void Server::Sync(bool SyncLinks)
{
	if (this->IsSynced())
		return;

	this->UnsetFlag(SERVER_SYNCING);

	if (SyncLinks && !this->Links.empty())
	{
		for (unsigned i = 0, j = this->Links.size(); i < j; ++i)
			this->Links[i]->Sync(true);
	}

	if (this->GetUplink() && this->GetUplink() == Me)
	{
		FOREACH_MOD(I_OnPreUplinkSync, OnPreUplinkSync(this));
		ircdproto->SendEOB();
		Me->Sync(false);
	}

	Log(this, "sync") << "is done syncing";

	FOREACH_MOD(I_OnServerSync, OnServerSync(this));

	if (this->GetUplink() && this->GetUplink() == Me)
	{
		FOREACH_MOD(I_OnUplinkSync, OnUplinkSync(this));
		for (channel_map::const_iterator it = ChannelList.begin(), it_end = ChannelList.end(); it != it_end; ++it)
			if (it->second->ci)
				it->second->ci->RestoreTopic();
	}
}

/** Check if this server is synced
 * @return true or false
 */
bool Server::IsSynced() const
{
	return !this->HasFlag(SERVER_SYNCING);
}

/** Check if this server is ULined
 * @return true or false
 */
bool Server::IsULined() const
{
	if (this == Me)
		return true;

	for (std::list<Anope::string>::const_iterator it = Config->Ulines.begin(), it_end = Config->Ulines.end(); it != it_end; ++it)
		if (it->equals_ci(this->GetName()))
			return true;
	return false;
}

/** Find a server
 * @param name The name or SID/numeric
 * @param s The server list to search for this server on, defaults to our Uplink
 * @return The server
 */
Server *Server::Find(const Anope::string &name, Server *s)
{
	Log(LOG_DEBUG) << "Server::Find called for " << name;

	if (!s)
		s = Me;
	if (s->GetName().equals_cs(name) || s->GetSID().equals_cs(name))
		return s;

	if (!s->GetLinks().empty())
	{
		for (unsigned i = 0, j = s->GetLinks().size(); i < j; ++i)
		{
			Server *serv = s->GetLinks()[i];

			if (serv->GetName().equals_cs(name) || serv->GetSID().equals_cs(name))
				return serv;
			Log(LOG_DEBUG) << "Server::Find checking " << serv->GetName() << " server tree for " << name;
			Server *server = Server::Find(name, serv);
			if (server)
				return server;
		}
	}

	return NULL;
}

/*************************************************************************/

/**
 * Handle adding the server to the Server struct
 * @param source Name of the uplink if any
 * @param servername Name of the server being linked
 * @param hops Number of hops to reach this server
 * @param descript Description of the server
 * @param numeric Server Numberic/SUID
 * @return void
 */
void do_server(const Anope::string &source, const Anope::string &servername, unsigned int hops, const Anope::string &descript, const Anope::string &numeric)
{
	if (source.empty())
		Log(LOG_DEBUG) << "Server " << servername << " introduced";
	else
		Log(LOG_DEBUG) << "Server introduced (" << servername << ")" << " from " << source;

	Server *s = NULL;

	if (!source.empty())
	{
		s = Server::Find(source);

		if (!s)
			throw CoreException("Recieved a server from a nonexistant uplink?");
	}
	else
		s = Me;

	/* Create a server structure. */
	Server *newserver = new Server(s, servername, hops, descript, numeric);

	/* Announce services being online. */
	if (Config->GlobalOnCycle && !Config->GlobalOnCycleUP.empty() && !Config->s_GlobalNoticer.empty())
		notice_server(Config->s_GlobalNoticer, newserver, "%s", Config->GlobalOnCycleUP.c_str());

	/* Let modules know about the connection */
	FOREACH_MOD(I_OnNewServer, OnNewServer(newserver));
}

/*************************************************************************/

/* TS6 UID generator common code.
 *
 * Derived from atheme-services, uid.c (hg 2954:116d46894b4c).
 *    -nenolod
 */
static bool ts6_uid_initted = false;
static char ts6_new_uid[10];

static void ts6_uid_increment(unsigned slot)
{
	if (slot != Config->Numeric.length())
	{
		if (ts6_new_uid[slot] == 'Z')
			ts6_new_uid[slot] = '0';
		else if (ts6_new_uid[slot] == '9')
		{
			ts6_new_uid[slot] = 'A';
			ts6_uid_increment(slot - 1);
		}
		else
			++ts6_new_uid[slot];
	}
	else
	{
		if (ts6_new_uid[slot] == 'Z')
			for (slot = 3; slot < 9; ++slot)
				ts6_new_uid[slot] = 'A';
		else
			++ts6_new_uid[slot];
	}
}

/** Recieve the next UID in our list
 * @return The UID
 */
const char *ts6_uid_retrieve()
{
	if (!ircd->ts6)
	{
		return "";
	}

	if (!ts6_uid_initted)
	{
		snprintf(ts6_new_uid, 10, "%sAAAAAA", Config->Numeric.c_str());
		ts6_uid_initted = true;
	}

	ts6_uid_increment(8);
	return ts6_new_uid;
}

/*******************************************************************/

/*
 * TS6 SID generator code, provided by DukePyrolator
 */

static bool ts6_sid_initted = false;
static char ts6_new_sid[4];

static void ts6_sid_increment(unsigned pos)
{
	/*
	 * An SID must be exactly 3 characters long, starts with a digit,
	 * and the other two characters are A-Z or digits
	 * The rules for generating an SID go like this...
	 * --> ABCDEFGHIJKLMNOPQRSTUVWXYZ --> 0123456789 --> WRAP
	 */
	if (!pos)
	{
		/* At pos 0, if we hit '9', we've run out of available SIDs,
		 * reset the SID to the smallest possible value and try again. */
		if (ts6_new_sid[pos] == '9')
		{
			ts6_new_sid[0] = '0';
			ts6_new_sid[1] = 'A';
			ts6_new_sid[2] = 'A';
		}
		else
			// But if we haven't, just keep incrementing merrily.
			++ts6_new_sid[0];
	}
	else
	{
		if (ts6_new_sid[pos] == 'Z')
			ts6_new_sid[pos] = '0';
		else if (ts6_new_sid[pos] == '9')
		{
			ts6_new_sid[pos] = 'A';
			ts6_sid_increment(pos - 1);
		}
		else
			++ts6_new_sid[pos];
	}
}

/** Return the next SID in our list
 * @return The SID
 */
const char *ts6_sid_retrieve()
{
	if (!ircd->ts6)
	{
		return "";
	}

	if (!ts6_sid_initted)
	{
		// Initialize ts6_new_sid with the services server SID
		snprintf(ts6_new_sid, 4, "%s", Config->Numeric.c_str());
		ts6_sid_initted = true;
	}

	while (1)
	{
		// Check if the new SID is used by a known server
		if (!Server::Find(ts6_new_sid))
			// return the new SID
			return ts6_new_sid;

		// Add one to the last SID
		ts6_sid_increment(2);
	}
	/* not reached */
	return "";
}