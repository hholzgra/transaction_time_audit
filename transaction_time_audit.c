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
  | Authors: Hartmut Holzgraefe <hartmut@mariadb.com>                    |
  +----------------------------------------------------------------------+
*/


/* TODO
 * query counter?
 * track first, last queries?
 * retrieve connection annotations?
 */


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

#define SESSION_VAR(THD, VAR) (((struct my_vars *)THDVAR(THD, session_data))->VAR)

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

/* remember this many queries per transaction */
static MYSQL_THDVAR_UINT(max_queries,
                         PLUGIN_VAR_RQCMDARG,
                         "remember this many queries per transaction",
                         /* check */ NULL,
                         /* update */ NULL,
                         /* default*/ 5,
                         /* min */ 3,
                         /* max */ UINT_MAX,
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
  MYSQL_SYSVAR(max_queries),
  NULL
};

/* private session data structure */
struct my_vars {
  MYSQL_XID xid;      /* previous events XID */
  char *user;         /* session user */
  time_t start_time;  /* start time of current transaction */
  unsigned long long query_counter; /* simple query counter counting MYSQL_AUDIT_GENERAL_LOG events */
  char **queries;     /* store log of actual queries here */
  unsigned max_queries; /* max. stored queries for current transaction */
};

/* SHOW_FUNC callback function that returns time the transaction started */
static int show_transaction_start(MYSQL_THD thd,
                                      struct st_mysql_show_var* var,
                                      char *buff)
{
  time_t t = (time_t)SESSION_VAR(thd, start_time);
  struct tm lt = *localtime(&t);

  sprintf(buff, "%4d-%02d-%02d %02d:%02d:%02d", 
          lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
          lt.tm_hour, lt.tm_min, lt.tm_sec);

  var->type  = SHOW_CHAR;
  var->value = buff;
  
  return 0;
}

/* SHOW_FUNC callback function that returns time elapsed since session start */
static int show_transaction_time(MYSQL_THD thd,
                                      struct st_mysql_show_var* var,
                                      char *buff)
{
  unsigned long long *result = (unsigned long long *)buff;

  var->type  = SHOW_LONGLONG;
  var->value = buff;
  
  if (SESSION_VAR(thd, start_time)) {
    *result = (unsigned long long)(time(NULL) - SESSION_VAR(thd, start_time));
  } else {
    *result = 0;
  }

  return 0;
}

/* SHOW_FUNC callback function that returns queries counted */
static int show_transaction_query_count(MYSQL_THD thd,
                                      struct st_mysql_show_var* var,
                                      char *buff)
{
  unsigned long long *result = (unsigned long long *)buff;

  var->type  = SHOW_LONGLONG;
  var->value = buff;
  
  *result = SESSION_VAR(thd, query_counter);

  return 0;
}

/* register SHOW STATUS variables and callbacks */
static struct st_mysql_show_var audit_plugin_statvars[]=
  {
    {"transaction_time_start", (char *)&show_transaction_start, SHOW_FUNC},
    {"transaction_time_span", (char *)&show_transaction_time, SHOW_FUNC},
    {"transaction_time_query_count", (char *)&show_transaction_query_count, SHOW_FUNC},
    {NULL, NULL, SHOW_UNDEF}
  };

/* called whenever we detect the start of a new transaction 
 *   
 * note that transactions actually start on the first real statement,
 * not on BEGIN / START TRANSACTION
 */
static void begin_transaction(MYSQL_THD thd, const struct mysql_event_general *event)
{
  /* populate session vars, 
     right now only 'user' is stored, 
     xid is initialized to zero by calloc already ... */
  if (THDVAR(thd, session_data)) {
    SESSION_VAR(thd, user) = (char *)calloc(1, event->general_user_length + 1);
    memcpy(SESSION_VAR(thd, user), event->general_user, event->general_user_length);

    /* remember transaction start time */
    SESSION_VAR(thd, start_time) = event->general_time;

    SESSION_VAR(thd, query_counter) = 1;

    SESSION_VAR(thd, max_queries) = THDVAR(thd, max_queries);
    SESSION_VAR(thd, queries) = (char **)calloc(SESSION_VAR(thd, max_queries), sizeof(char *));

    *SESSION_VAR(thd, queries) = (char *)calloc(1, event->general_query_length +1);
    memcpy(*SESSION_VAR(thd, queries), event->general_query, event->general_query_length);
  }
}

/* called whenever we detect that a transaction ended */
static void end_transaction(MYSQL_THD thd) 
{
  /* simple logging */
  if (THDVAR(thd, session_data) && SESSION_VAR(thd, user)) {
    unsigned long time_taken = (unsigned long)(time(NULL) - SESSION_VAR(thd, start_time)); 
    char **queries = SESSION_VAR(thd, queries);
    int i;
   
    if ((THDVAR(thd, limit)) && (time_taken >= THDVAR(thd, limit))) {
      time_t t = time(NULL);
      struct tm lt = *localtime(&t);
      int first = SESSION_VAR(thd, max_queries) / 2;

      fprintf(stderr, "%2.2d%2.2d%2.2d %2.2d:%2.2d:%2.2d [note] long transaction: user: '%s', time taken: %lu, queries: %lu\n", 
              lt.tm_year % 100, lt.tm_mon, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec,
              SESSION_VAR(thd, user), time_taken, (unsigned long)SESSION_VAR(thd, query_counter));
      
      for (i = 0; i < SESSION_VAR(thd, max_queries); i++) {
        if (queries[i]) {
          if ((i == first) && (SESSION_VAR(thd, query_counter) > SESSION_VAR(thd, max_queries))) {
            fprintf(stderr, "[... %d more queries ...]\n", (int)(SESSION_VAR(thd, query_counter) - SESSION_VAR(thd, max_queries)));
          }
          
          fprintf(stderr, "  %-80s\n", queries[i]);
        }
      }
    }

    SESSION_VAR(thd, start_time) = 0;
    SESSION_VAR(thd, query_counter) = 0;

    for (i = 0; i < SESSION_VAR(thd, max_queries); i++) {
      if (queries[i]) {
        free(queries[i]);
      }
    }

    free(queries);

    SESSION_VAR(thd, queries) = NULL;
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
          SESSION_VAR(thd, xid).formatID = -1;
          SESSION_VAR(thd, start_time) = 0;
          SESSION_VAR(thd, query_counter) = 0;
        }
        break;

        // connection ended: clean up
      case MYSQL_AUDIT_CONNECTION_DISCONNECT:
        end_transaction(thd);
        if (THDVAR(thd, session_data)) {
          int i;
          char **queries = SESSION_VAR(thd, queries);

          if (SESSION_VAR(thd, user)) {
            free(SESSION_VAR(thd, user));
          }
          
          for (i = 0; i < SESSION_VAR(thd, max_queries); i++) {
            if (queries[i]) {
              free(queries[i]);
            }
          }

          free(queries);
          free((void *)THDVAR(thd, session_data));
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
        old_xid = &SESSION_VAR(thd, xid);

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

        // simple query counter
        if ((event_general->event_subclass == MYSQL_AUDIT_GENERAL_LOG) 
            && !strncmp(event_general->general_command, "Query", event_general->general_command_length)
            && SESSION_VAR(thd, start_time)) {
          int i;
          char **queries = SESSION_VAR(thd, queries);
          char *query;

          i = SESSION_VAR(thd, query_counter)++;

          query = (char*)calloc(event_general->general_query_length + 1, 1);
          memcpy(query, event_general->general_query, event_general->general_query_length);

          
          if ( i < SESSION_VAR(thd, max_queries)) {
            queries[i] = query;
          } else {
            int j, first = SESSION_VAR(thd, max_queries) / 2;
            
            free(queries[first]);
            for (j = first; j < SESSION_VAR(thd, max_queries) - 1; j++) {
              queries[j] = queries[j + 1];
            }
            queries[SESSION_VAR(thd, max_queries) - 1] = query;
          }
        }
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
