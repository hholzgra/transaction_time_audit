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

#ifndef MYSQL_SERVICE_MY_PLUGIN_LOG_INCLUDED

#endif

/* log long running transactions that took more than this many seconds */
static MYSQL_THDVAR_ULONGLONG(limit,
                              PLUGIN_VAR_RQCMDARG,
                              "log transactions that took longer than this many seconds",
                              /* check */ NULL,
                              /* update */ NULL,
                              /* default*/ 10,
                              /* min */ 0,
                              /* max */ ULLONG_MAX,
                              /* blocksize */ 1
                              );

/* this is an internal per-connection structure only,
 so NOCMDOPT and NOSYSVAR hide it in --help and 
 SHOW VARIABLES output */

static MYSQL_THDVAR_ULONGLONG(session_data,
                              PLUGIN_VAR_READONLY | PLUGIN_VAR_NOCMDOPT | PLUGIN_VAR_NOSYSVAR,
                              "private structure pointer",
                              /* check */ NULL,
                              /* update */ NULL,
                              /* default*/ 0,
                              /* min */ 0,
                              /* max */ ULLONG_MAX,
                              /* blocksize */ 1
                              );

/* register session variables declared above */
static struct st_mysql_sys_var* audit_plugin_sysvars[] = {
  MYSQL_SYSVAR(limit),
  MYSQL_SYSVAR(session_data),
  NULL
};

/* private session data structure */
struct my_vars {
  MYSQL_XID xid;
  char *user;
  time_t start_time;
};

/* SHOW_FUNC callback function that returns time the transaction started */
static int show_transaction_start(MYSQL_THD thd,
                                      struct st_mysql_show_var* var,
                                      char *buff)
{
  unsigned long long *result = (unsigned long long *)buff;
  struct my_vars *session_vars = (struct my_vars *)THDVAR(thd, session_data);

  var->type  = SHOW_LONGLONG;
  var->value = buff;
  
  *result = (unsigned long long)(session_vars->start_time);

  return 0;
}

/* SHOW_FUNC callback function that returns time elapsed since session start */
static int show_transaction_time(MYSQL_THD thd,
                                      struct st_mysql_show_var* var,
                                      char *buff)
{
  unsigned long long *result = (unsigned long long *)buff;
  struct my_vars *session_vars = (struct my_vars *)THDVAR(thd, session_data);

  var->type  = SHOW_LONGLONG;
  var->value = buff;
  
  if (session_vars->start_time) {
    *result = time(NULL) - session_vars->start_time;
  } else {
    *result = 0;
  }

  return 0;
}

/* register SHOW STATUS variables and callbacks */
static struct st_mysql_show_var audit_plugin_statvars[]=
  {
    {"transaction_time_start", (char *)&show_transaction_start, SHOW_FUNC},
    {"transaction_time_span", (char *)&show_transaction_time, SHOW_FUNC},
    {NULL, NULL, SHOW_UNDEF}
  };

/* called whenever we detect the start of a new transaction 
 *   
 * note that transactions actually start on the first real statement,
 * not on BEGIN / START TRANSACTION
 */
static void begin_transaction(MYSQL_THD thd, const struct mysql_event_general *event)
{
  struct my_vars *session_vars = (struct my_vars *)THDVAR(thd, session_data);

  /* populate session vars, 
     right now only 'user' is stored, 
     xid is initialized to zero by calloc already ... */
  if (session_vars) {
    session_vars->user = (char *)calloc(1, event->general_user_length + 1);
    memcpy(session_vars->user, event->general_user, event->general_user_length);
    session_vars->start_time = event->general_time;

    /* remember transaction start time */
    session_vars->start_time = event->general_time;
  }
}

/* called whenever we detect that a transaction ended */
static void end_transaction(MYSQL_THD thd) 
{
  struct my_vars *session_vars = (struct my_vars *)THDVAR(thd, session_data);

  // simple logging 
  if (session_vars && session_vars->user) {
    unsigned long long time_taken = (unsigned long long)(time(NULL) - session_vars->start_time);
    if ((THDVAR(thd, limit)) && (time_taken >= THDVAR(thd, limit))) {
      fprintf(stderr, "transaction ended, user: %s, time taken: %Lu\n", 
                session_vars->user, time_taken);
    }
    session_vars->start_time = 0;
  }

}

/* the actual audit notify callback */
static void audit_notify(MYSQL_THD thd, unsigned int event_class, const void *event)
{   
  /* act depending on event class */
  switch (event_class) {

  /* client connect / disconnect */
  case MYSQL_AUDIT_CONNECTION_CLASS:
    {
      /* convert to correct event data pointer */
      const struct mysql_event_connection *event_connection=
        (const struct mysql_event_connection *) event;

      switch (event_connection->event_subclass) {
        
        // new connection / session: allocate and initialize session data
      case MYSQL_AUDIT_CONNECTION_CONNECT:  
        THDVAR(thd, session_data) = (unsigned long long)calloc(sizeof(struct my_vars), 1); 
        if (THDVAR(thd, session_data)) {
          struct my_vars *session_vars = (struct my_vars *)THDVAR(thd, session_data);
          session_vars->xid.formatID = -1;
          session_vars->start_time = 0;
        }
        break;

        // connection ended: clean up
      case MYSQL_AUDIT_CONNECTION_DISCONNECT:
        end_transaction(thd);
        if (THDVAR(thd, session_data)) {
          struct my_vars *session_vars = (struct my_vars *)THDVAR(thd, session_data);
          if (session_vars->user) {
            free(session_vars->user);
          }
          free(session_vars);
        }
        break;
      }
    }
    break;

  /* handle events generated by SQL processing */
  case MYSQL_AUDIT_GENERAL_CLASS:
    {
      MYSQL_XID xid, *old_xid;

      /* convert to correct event data pointer */
      const struct mysql_event_general *event_general=
        (const struct mysql_event_general *) event;
      
      /* we don't distinguish between the various event
         subtypes here, we're just tracking XID value
         changes and act accordingly */
      if (THDVAR(thd, session_data)) {
        // get current and previous transaction IDs
        thd_get_xid(thd, &xid);
        old_xid = &((struct my_vars *)THDVAR(thd, session_data))->xid;

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
          } else if (memcmp(old_xid, &xid, sizeof(MYSQL_XID))) {
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

/* audit specific plugin setup */
static struct st_mysql_audit audit_descriptor=
  {
    MYSQL_AUDIT_INTERFACE_VERSION,
    NULL,
    audit_notify,
    { MYSQL_AUDIT_GENERAL_CLASSMASK | MYSQL_AUDIT_CONNECTION_CLASSMASK }
  };

/* general plugin setup */
mysql_declare_plugin(audit)
{
  MYSQL_AUDIT_PLUGIN,
    &audit_descriptor, 
    "transaction_time",
    "Hartmut Holzgraefe <hartmut@mariadb.com>",
    "A simple audit plugin measuring transaction times",
    PLUGIN_LICENSE_GPL,
    NULL,
    NULL,
    0x0001,
    audit_plugin_statvars,
    audit_plugin_sysvars,
    NULL, /* placeholder for command line options, not available yet */
    }
mysql_declare_plugin_end;

#ifdef MARIA_PLUGIN_INTERFACE_VERSION

/* general plugin setup, MariaDB version */
maria_declare_plugin(audit)
{
  MYSQL_AUDIT_PLUGIN,
    &audit_descriptor, 
    "transaction_time",
    "Hartmut Holzgraefe <hartmut@mariadb.com>",
    "A simple audit plugin measuring transaction times",
    PLUGIN_LICENSE_GPL,
    NULL,
    NULL,
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
