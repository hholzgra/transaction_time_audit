MySQL/MariaDB +transaction_time+ audit plugin
=============================================

This plugin logs long running transactions to the server
error log when transaction took more than a configurable
number of seconds.

Installation & Activation
-------------------------

See INSTALL file

Configuration variables
-----------------------

+transaction_time_limit+
~~~~~~~~~~~~~~~~~~~~~~~~

Log long running transaction to error log if it took more
than this many seconds (default=10s, disabled if set to 0s)

+transaction_time_max_queries+
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Number of queries to remember and put into log messages.
If there are more queries in a transaction about half of
the first and last queries each will be logged while 
queries in the middle of the transaction will be discarded.

STATUS values
-------------

+transaction_time_start+
~~~~~~~~~~~~~~~~~~~~~~~~

Time the current transaction was started as a Unix Timestamp
(seconds since 1970), or 0 if not currently in an active 
transaction.

Note: a transaction doesn't actually start with BEGIN or
START TRANSACTION but with the first actual statement touching
data after that

+transaction_time_span+
~~~~~~~~~~~~~~~~~~~~~~~

Time in seconds since the transaction started or 0 if not in
an active transaction.

+transaction_time_query_count+
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Number of queries executed since transaction start.

