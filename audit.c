/*
  +----------------------------------------------------------------------+
  | This program is free software; you can redistribute it and/or        |
  | modify it under the terms of the GNU General Public License          |
  | as published by the Free Software Foundation; either version 2       |
  | of the License, or (at your option) any later version.               |
  |                                                                      |
  | This program is distributed in the hope that it will be useful,      |
  | but WITHOUT ANY WARRANTY; without even the implied warranty of       |
  | MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        |
  | GNU General Public License for more details.                         |
  |                                                                      |
  | You should have received a copy of the GNU General General Public    |
  | License in the file LICENSE along with this library;                 |
  | if not, write to the                                                 |
  |                                                                      |
  |   Free Software Foundation, Inc.,                                    |
  |   51 Franklin Street, Fifth Floor,                                   |
  |   Boston, MA  02110-1301  USA                                        |
  +----------------------------------------------------------------------+
  | Authors: Hartmut Holzgraefe <hartmut@skysql.com>                     |
  +----------------------------------------------------------------------+
*/

/* $ Id: $ */ 


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>

#include <mysql_version.h>

#include <mysql/plugin_audit.h>

#include <mysql/services.h>
#include <sql_class.h>


#ifndef MYSQL_SERVICE_MY_PLUGIN_LOG_INCLUDED

#endif

#ifndef PLUGIN_VAR_HIDDEN
#define PLUGIN_VAR_HIDDEN 0
#endif

static MYSQL_THDVAR_ULONGLONG(xidptr,
															PLUGIN_VAR_READONLY | PLUGIN_VAR_NOCMDOPT | PLUGIN_VAR_HIDDEN,
															"XID structure pointer",
															/* check */ NULL,
															/* update */ NULL,
															/* default*/ 0,
															/* min */ 0,
															/* max */ ULLONG_MAX,
															/* blocksize */ 1
															);

static MYSQL_THDVAR_ULONGLONG(trans_start_time,
															PLUGIN_VAR_READONLY | PLUGIN_VAR_NOCMDOPT | PLUGIN_VAR_HIDDEN,
															"transaction start time",
															/* check */ NULL,
															/* update */ NULL,
															/* default*/ 0,
															/* min */ 0,
															/* max */ ULLONG_MAX,
															/* blocksize */ 1
															);

static st_mysql_sys_var* audit_plugin_sysvars[] = {
 	MYSQL_SYSVAR(xidptr),
	MYSQL_SYSVAR(trans_start_time),
	NULL
};

struct my_vars {
	MYSQL_XID xid;
	char *user;
	STATUS_VAR begin_status;
};

static int show_transaction_time(MYSQL_THD thd,
                                      struct st_mysql_show_var* var,
                                      char *buff)
{
	unsigned long long *result = (unsigned long long *)buff;

	var->type  = SHOW_ULONGLONG;
	var->value = buff;
	
	if (THDVAR(thd, trans_start_time)) {
		*result = time(NULL) - THDVAR(thd, trans_start_time);
	} else {
		*result = 0;
	}
}

static struct st_mysql_show_var audit_plugin_statvars[]=
	{
		{"transaction_time_span", (char *)&show_transaction_time, SHOW_FUNC},
		{NULL, NULL, SHOW_UNDEF}
	};

static void begin_transaction(MYSQL_THD thd, const struct mysql_event_general *event)
{
	struct my_vars *m;

	m = (struct my_vars *)THDVAR(thd, xidptr);

	if (m) {
	  m->user = (char *)calloc(1, event->general_user_length + 1);
	  memcpy(m->user, event->general_user, event->general_user_length);

		memcpy(m->begin_status, ((THD *)thd)->status_vars, sizeof(STATUS_VAR));
	}

	THDVAR(thd, trans_start_time) = event->general_time;
}

static void end_transaction(MYSQL_THD thd) 
{
	struct my_vars *m;
	m = (struct my_vars *)THDVAR(thd, xidptr);

	if (m && m->user) {
	  fprintf(stderr, "transaction ended, user: %s, time taken: %Lu\n", m->user, time(NULL) - THDVAR(thd, trans_start_time));
	}

	THDVAR(thd, trans_start_time) = 0;
}

static int cmp_xid(const MYSQL_XID *x1, const MYSQL_XID *x2)
{
	memcmp(x1, x2, sizeof(MYSQL_XID));
}

static int audit_plugin_init(void *data)
{
	return 0;
}

static int audit_plugin_deinit(void *data)
{
	return 0;
}

static void audit_release(MYSQL_THD THD)
{   
} 

static void audit_notify(MYSQL_THD thd, unsigned int event_class, const void *event)
{   
	switch (event_class) {

	case MYSQL_AUDIT_CONNECTION_CLASS:
		{
			const struct mysql_event_connection *event_connection=
				(const struct mysql_event_connection *) event;

			switch (event_connection->event_subclass) {

			case MYSQL_AUDIT_CONNECTION_CONNECT:				
				THDVAR(thd, xidptr) = (unsigned long long)calloc(sizeof(struct my_vars), 1); 
				if (THDVAR(thd, xidptr)) {
					((struct my_vars *)THDVAR(thd, xidptr))->xid.formatID = -1;
				}
				THDVAR(thd, trans_start_time) = 0;
				break;

			case MYSQL_AUDIT_CONNECTION_DISCONNECT:
				end_transaction(thd);
				if (THDVAR(thd, xidptr)) {
					struct my_vars *m = (struct my_vars *)THDVAR(thd, xidptr);
					free(m->user);
					free(m);
				}
				break;
			}
		}
		break;

	case MYSQL_AUDIT_GENERAL_CLASS:
		{
			const struct mysql_event_general *event_general=
				(const struct mysql_event_general *) event;
			
			MYSQL_XID xid, *old_xid;

			if (THDVAR(thd, xidptr)) {
				// get current and previous transaction IDs
				thd_get_xid(thd, &xid);
				old_xid = &((struct my_vars *)THDVAR(thd, xidptr))->xid;

				// a transaction is valid if its formatID is >= 0

				if (xid.formatID == -1) {
					// currently not in a valid transaction
					if (old_xid->formatID >= 0) {
						// transition from valid to invalid transaction ID -> transaction ended
						end_transaction(thd);
					}
				} else {
					// currently in a valid transaction
					if (old_xid->formatID == -1) {
						// previously not in a transaction -> transaction started
						begin_transaction(thd, event_general);
					} else if (cmp_xid(old_xid, &xid)) {
						// previously in a valid transaction, too, and transaction ID changed
						// old transaction ended, new one started
						end_transaction(thd);
						begin_transaction(thd, event_general);
					}
					// else: still in same transaction
				}

				// remember current transaction id
				memcpy(old_xid, &xid, sizeof(MYSQL_XID));
			}
		}
		break;
	}

} 

static struct st_mysql_audit audit_descriptor=
	{
		MYSQL_AUDIT_INTERFACE_VERSION,
		audit_release,
		audit_notify,
		{ MYSQL_AUDIT_GENERAL_CLASSMASK | MYSQL_AUDIT_CONNECTION_CLASSMASK }
	};


mysql_declare_plugin(audit)
{
	MYSQL_AUDIT_PLUGIN,
		&audit_descriptor, 
		"transaction_time",
		"Hartmut Holzgraefe <hartmut@skysql.com>",
		"A simple audit plugin measuring transaction times",
		PLUGIN_LICENSE_GPL,
		audit_plugin_init,
		audit_plugin_deinit,
		0x0001,
		audit_plugin_statvars,
		audit_plugin_sysvars,
		NULL, /* placeholder for command line options, not available yet */
		}
mysql_declare_plugin_end;

#ifdef MARIA_PLUGIN_INTERFACE_VERSION

maria_declare_plugin(audit)
{
	MYSQL_AUDIT_PLUGIN,
		&audit_descriptor, 
		"transaction_time",
		"Hartmut Holzgraefe <hartmut@skysql.com>",
		"A simple audit plugin measuring transaction times",
		PLUGIN_LICENSE_GPL,
		audit_plugin_init,
		audit_plugin_deinit,
		0x0001,
		audit_plugin_statvars,
		audit_plugin_sysvars,
		"0.1", /* version string */
		MariaDB_PLUGIN_MATURITY_BETA,
		}
maria_declare_plugin_end;

#endif
/*
 * Local variables:
 * tab-width: 2
 * c-basic-offset: 2
 * End:
 * vim600: noet sw=2 ts=2 fdm=marker
 ** vim<600: noet sw=2 ts=2
 */
