MySQL/MariaDB transaction_time audit plugin
===========================================

This plugin logs long running transactions to the server
error log when transaction took more than a configurable
number of seconds.

Installation & Activation
-------------------------

See INSTALL file

Configuration variables
-----------------------

transaction_time_limit
^^^^^^^^^^^^^^^^^^^^^^

Log long running transaction to error log if it took more
than this many seconds (default=10s, disabled if set to 0s)

STATUS values
-------------

transaction_time_start
^^^^^^^^^^^^^^^^^^^^^^

Time the current transaction was started as a Unix Timestamp
(seconds since 1970), or 0 if not currently in an active 
transaction.

Note: a transaction doesn't actually start with BEGIN or
START TRANSACTION but with the first actual statement touching
data after that

transaction_time_span
^^^^^^^^^^^^^^^^^^^^^

Time in seconds since the transaction started or 0 if not in
an active transaction.

