/*
 * Anope IRC Services
 *
 * Copyright (C) 2014-2017 Anope Team <team@anope.org>
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

namespace Event
{
	struct CoreExport DeleteVhost : Events
	{
		static constexpr const char *NAME = "deletevhost";

		using Events::Events;

		/**
		 * Called when all of a users vhosts are being deleted
		 * @param source The user deleting the vhost
		 * @param account The account the vhost is being deleted from
		 */
		virtual void OnDeleteAllVhost(CommandSource *source, NickServ::Account *account) anope_abstract;

		/** Called when a vhost is deleted
		 * @param source The user deleting the vhost
		 * @param account The account the vhost is being deleted from
		 * @param vhost The vhost being deleted
		 */
		virtual void OnDeleteVhost(CommandSource *source, NickServ::Account *account, HostServ::VHost *vhost) anope_abstract;
	};
}

