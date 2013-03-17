/*
 * gluff - eat DHCPACK events from a queue table written to by dhcpd, and write out complete
 *         lease records to a mysql database.

Copyright (c) 2008-2009, Hans Liss <Hans@Liss.pp.se>.

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>
#include <sqlite3.h>
#include <mysql/mysql.h>

/* Local SQL queries for sqlite3 */

#define RESET_LSQL "UPDATE lease_queue set claimed=0"
#define CLAIM_LSQL "UPDATE lease_queue set claimed=? where claimed=0"
#define GET_LSQL "SELECT start,rtype,end,ip,hw,cid,rid FROM lease_queue where claimed=? order by start,idx"
#define CLEAR_LSQL "DELETE FROM lease_queue where claimed=?"

/* Remote SQL queries for the MySQL database */
#define GETCID_RSQL "SELECT id from cids where value=?"
#define GETRID_RSQL "SELECT id from rids where value=?"
#define GETIP_RSQL "SELECT id from ips where value=?"
#define GETHW_RSQL "SELECT id from hws where value=?"

#define MAKECID_RSQL "INSERT INTO cids (value) values (?)"
#define MAKERID_RSQL "INSERT INTO rids (value) values (?)"
#define MAKEIP_RSQL "INSERT INTO ips (value) values (?)"
#define MAKEHW_RSQL "INSERT INTO hws (value) values (?)"

#define FIND_LEASE_RSQL "SELECT lstart,lend,hw,cid,rid from leases where ip=? and lstart<=? and lend>=?"
#define CUTOFF_LEASE_RSQL "UPDATE leases set lend=? where ip=? and lstart<=? and lend>=?"
#define PROLONG_LEASE_RSQL "UPDATE leases set lend=? where ip=? and lstart<=? and lend<? and lend>=?"
#define REMOVE_LEASE_RSQL "DELETE from leases where ip=? and lstart<=? and lend>=?"
#define MAKE_LEASE_RSQL "REPLACE INTO leases (ip,lstart,lend,hw,cid,rid) values (?,?,?,?,?,?)"

#define max(a,b) ((b)>(a)?(b):(a))
#define min(a,b) ((b)<(a)?(b):(a))

int gluffdebug=0;

/* Print usage text */
void usage(char *progname) {
  fprintf(stderr, "Usage: %s -l <local db file> -h <remote db host> -u <remote db user>\n", progname);
  fprintf(stderr, "\t-p <remote db password> -d <remote db database>\n");
  fprintf(stderr, "\t[-R (reset claims)] [-F (do not fork)] [-Q (be quiet)] [-P <pidfilename>] [-D (debug)]\n");
}

int do_make_lease(MYSQL *db, int ip, time_t start, time_t end, int hw, int cid, int rid);


/* Get a numeric id from one of the lexical tables, creating a new record if none exists */
int get_id(MYSQL *db, const unsigned char *val, char *getq, char *setq) {
  MYSQL_STMT *stmt=NULL;
  MYSQL_BIND param[1], result[1];
  unsigned long blen;
  int id;
  if ((stmt = mysql_stmt_init(db)) == NULL) {
    syslog(LOG_ERR, "mysql_stmt_init(): %s", mysql_error(db));
    return 0;
  }

  if (mysql_stmt_prepare(stmt, getq, strlen(getq)) != 0) {
    syslog(LOG_ERR, "mysql_stmt_prepare(): %s", mysql_error(db));
    return 0;
  }

  memset ((void *) param, 0, sizeof (param));

  param[0].buffer_type = MYSQL_TYPE_STRING;
  param[0].buffer = (void *)val;
  blen=strlen((char *)val);
  param[0].buffer_length = blen+1;
  param[0].length = &blen;
  param[0].is_null = 0;

  if (mysql_stmt_bind_param(stmt, param) != 0) {
    syslog(LOG_ERR, "mysql_bind_param(): %s", mysql_error(db));
    return 0;
  }

  memset ((void *) result, 0, sizeof (result));

  result[0].buffer_type = MYSQL_TYPE_LONG;
  result[0].buffer = (void *)&id;
  result[0].is_unsigned = 0;
  result[0].is_null = 0;

  if (mysql_stmt_bind_result(stmt, result) != 0) {
    syslog(LOG_ERR, "mysql_bind_result(): %s", mysql_error(db));
    return 0;
  }

  if (mysql_stmt_execute(stmt) != 0) {
    syslog(LOG_ERR, "mysql_execute(): %s", mysql_error(db));
    return 0;
  }

  if (mysql_stmt_store_result(stmt) != 0) {
    syslog(LOG_ERR, "mysql_store_result(): %s", mysql_error(db));
    return 0;
  }
  
  if (mysql_stmt_num_rows(stmt) == 1) {
    mysql_stmt_fetch(stmt);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return id;
  }
  
  mysql_stmt_free_result(stmt);
  mysql_stmt_close(stmt);

  if ((stmt = mysql_stmt_init(db)) == NULL) {
    syslog(LOG_ERR, "mysql_stmt_init(): %s", mysql_error(db));
    return 0;
  }
  if (mysql_stmt_prepare(stmt, setq, strlen(setq)) != 0) {
    syslog(LOG_ERR, "mysql_stmt_prepare(): %s", mysql_error(db));
    return 0;
  }

  memset ((void *) param, 0, sizeof (param));
  param[0].buffer_type = MYSQL_TYPE_STRING;
  param[0].buffer = (void *)val;
  blen=strlen((char *)val);
  param[0].buffer_length = blen+1;
  param[0].length = &blen;
  param[0].is_null = 0;


  if (mysql_stmt_bind_param(stmt, param) != 0) {
    syslog(LOG_ERR, "mysql_bind_param(): %s", mysql_error(db));
    return 0;
  }

  if (mysql_stmt_execute(stmt) != 0) {
    syslog(LOG_ERR, "mysql_execute(): %s", mysql_error(db));
    return 0;
  }

  id=mysql_stmt_insert_id(stmt);
  mysql_stmt_close(stmt);
  return id;
}

void mytime2tm(MYSQL_TIME *mtt, struct tm *tmt) {
  tmt->tm_year = mtt->year - 1900;
  tmt->tm_mon = mtt->month - 1;
  tmt->tm_mday = mtt->day;
  tmt->tm_hour = mtt->hour;
  tmt->tm_min = mtt->minute;
  tmt->tm_sec = mtt->second;
}

time_t mytime2timet(MYSQL_TIME *mtt) {
  struct tm tm_tmp;
  mytime2tm(mtt, &tm_tmp);
  return mktime(&tm_tmp);
}

void tm2mytime(struct tm *tmt, MYSQL_TIME *mtt) {
  mtt->year = tmt->tm_year + 1900;
  mtt->month = tmt->tm_mon + 1;
  mtt->day = tmt->tm_mday;
  mtt->hour = tmt->tm_hour;
  mtt->minute = tmt->tm_min;
  mtt->second = tmt->tm_sec;
  mtt->second_part = 0;
  mtt->neg = 0;
}

void timet2mytime(time_t t, MYSQL_TIME *mtt) {
  struct tm *tm_tmp=localtime(&t);
  tm2mytime(tm_tmp, mtt);
}

int rdb_cid_id(MYSQL *db, const unsigned char *val) {
  return get_id(db, val, GETCID_RSQL, MAKECID_RSQL);
}

int rdb_rid_id(MYSQL *db, const unsigned char *val) {
  return get_id(db, val, GETRID_RSQL, MAKERID_RSQL);
}

int rdb_ip_id(MYSQL *db, const unsigned char *val) {
  return get_id(db, val, GETIP_RSQL, MAKEIP_RSQL);
}

int rdb_hw_id(MYSQL *db, const unsigned char *val) {
  return get_id(db, val, GETHW_RSQL, MAKEHW_RSQL);
}

/* Replace multiple overlapping leases with a single new one */
int do_replace_leases(MYSQL *db, int ip, time_t searchtime, time_t start, time_t end, int hw, int cid, int rid) {
  MYSQL_STMT *stmt=NULL;
  MYSQL_BIND param[5];
    
  MYSQL_TIME my_searchtime;

  timet2mytime(searchtime, &my_searchtime);

  if ((stmt = mysql_stmt_init(db)) == NULL) {
    syslog(LOG_ERR, "mysql_stmt_init(): %s", mysql_error(db));
    return -1;
  }

  if (mysql_stmt_prepare(stmt, REMOVE_LEASE_RSQL, strlen(REMOVE_LEASE_RSQL)) != 0) {
    syslog(LOG_ERR, "mysql_stmt_prepare(): %s", mysql_error(db));
    return -1;
  }

  memset ((void *) param, 0, sizeof (param));

  param[0].buffer_type = MYSQL_TYPE_LONG;
  param[0].buffer = (void *)&ip;
  param[0].is_unsigned = 0;
  param[0].is_null = 0;

  param[1].buffer_type = MYSQL_TYPE_DATETIME;
  param[1].buffer = (void *) &my_searchtime;
  param[1].is_null = 0;

  param[2].buffer_type = MYSQL_TYPE_DATETIME;
  param[2].buffer = (void *) &my_searchtime;
  param[2].is_null = 0;

  if (mysql_stmt_bind_param(stmt, param) != 0) {
    syslog(LOG_ERR, "mysql_bind_param(): %s", mysql_error(db));
    return -1;
  }

  if (mysql_stmt_execute(stmt) != 0) {
    syslog(LOG_ERR, "mysql_execute(): %s", mysql_error(db));
    return -1;
  }
 
  mysql_stmt_close(stmt);

  return do_make_lease(db, ip, start, end, hw, cid, rid);
}


/* Try to find an active lease for the IP address in question, and return all the data */
int do_find_lease(MYSQL *db, int ip, time_t start, time_t *thatstart, time_t *thatend,
		  int *thathw, int *thatcid, int *thatrid) {
  MYSQL_STMT *stmt=NULL;
  MYSQL_BIND param[3], result[5];
    
  MYSQL_TIME my_start;
  MYSQL_TIME my_thatstart, my_thatend;

  timet2mytime(start, &my_start);

  if ((stmt = mysql_stmt_init(db)) == NULL) {
    syslog(LOG_ERR, "mysql_stmt_init(): %s", mysql_error(db));
    return -1;
  }

  if (mysql_stmt_prepare(stmt, FIND_LEASE_RSQL, strlen(FIND_LEASE_RSQL)) != 0) {
    syslog(LOG_ERR, "mysql_stmt_prepare(): %s", mysql_error(db));
    return -1;
  }

  memset ((void *) param, 0, sizeof (param));

  param[0].buffer_type = MYSQL_TYPE_LONG;
  param[0].buffer = (void *)&ip;
  param[0].is_unsigned = 0;
  param[0].is_null = 0;

  param[1].buffer_type = MYSQL_TYPE_DATETIME;
  param[1].buffer = (void *) &my_start;
  param[1].is_null = 0;

  param[2].buffer_type = MYSQL_TYPE_DATETIME;
  param[2].buffer = (void *) &my_start;
  param[2].is_null = 0;

  if (mysql_stmt_bind_param(stmt, param) != 0) {
    syslog(LOG_ERR, "mysql_bind_param(): %s", mysql_error(db));
    return -1;
  }

  memset ((void *) result, 0, sizeof (result));

  result[0].buffer_type = MYSQL_TYPE_DATETIME;
  result[0].buffer = (void *)&my_thatstart;
  result[0].is_null = 0;

  result[1].buffer_type = MYSQL_TYPE_DATETIME;
  result[1].buffer = (void *)&my_thatend;
  result[1].is_null = 0;

  result[2].buffer_type = MYSQL_TYPE_LONG;
  result[2].buffer = (void *)thathw;
  result[2].is_unsigned = 0;
  result[2].is_null = 0;

  result[3].buffer_type = MYSQL_TYPE_LONG;
  result[3].buffer = (void *)thatcid;
  result[3].is_unsigned = 0;
  result[3].is_null = 0;

  result[4].buffer_type = MYSQL_TYPE_LONG;
  result[4].buffer = (void *)thatrid;
  result[4].is_unsigned = 0;
  result[4].is_null = 0;

  if (mysql_stmt_bind_result(stmt, result) != 0) {
    syslog(LOG_ERR, "mysql_bind_result(): %s", mysql_error(db));
    return -1;
  }

  if (mysql_stmt_execute(stmt) != 0) {
    syslog(LOG_ERR, "mysql_execute(): %s", mysql_error(db));
    return -1;
  }

  if (mysql_stmt_store_result(stmt) != 0) {
    syslog(LOG_ERR, "mysql_store_result(): %s", mysql_error(db));
    return -1;
  }

  if (mysql_stmt_num_rows(stmt) >= 1) {
    mysql_stmt_fetch(stmt);

    *thatstart = mytime2timet(&my_thatstart);
    *thatend = mytime2timet(&my_thatend);

    if (mysql_stmt_num_rows(stmt) > 1) {
      if (gluffdebug) {
	syslog(LOG_DEBUG,"Multiple rows exist. Compacting...");
      }
      while (mysql_stmt_fetch(stmt)) {
	*thatstart = min(*thatstart, mytime2timet(&my_thatstart));
	*thatend = max(*thatstart, mytime2timet(&my_thatend));
      }
      mysql_stmt_free_result(stmt);
      mysql_stmt_close(stmt);
      //      if (do_replace_leases(db, ip, start, *thatstart, *thatend, *thathw, *thatcid, *thatrid) != 0) return -17;
    } else {
      mysql_stmt_free_result(stmt);
      mysql_stmt_close(stmt);
    }

    return 1;
  }

  mysql_stmt_free_result(stmt);
  mysql_stmt_close(stmt);
  return 0;
}

/* Change the 'end time' for a lease */
int do_update_lease(MYSQL *db, int ip, time_t thatstart, time_t thatend, time_t newend, int prolong) {
  MYSQL_STMT *stmt=NULL;
  MYSQL_BIND param[5];
    
  MYSQL_TIME my_thatstart, my_thatend;
  MYSQL_TIME my_newend;

  timet2mytime(thatstart, &my_thatstart);
  timet2mytime(thatend, &my_thatend);
  timet2mytime(newend, &my_newend);

  if ((stmt = mysql_stmt_init(db)) == NULL) {
    syslog(LOG_ERR, "mysql_stmt_init(): %s", mysql_error(db));
    return -1;
  }

  if (prolong) {
    if (mysql_stmt_prepare(stmt, PROLONG_LEASE_RSQL, strlen(PROLONG_LEASE_RSQL)) != 0) {
      syslog(LOG_ERR, "mysql_stmt_prepare(): %s", mysql_error(db));
      return -1;
    }
  } else {
    if (mysql_stmt_prepare(stmt, CUTOFF_LEASE_RSQL, strlen(CUTOFF_LEASE_RSQL)) != 0) {
      syslog(LOG_ERR, "mysql_stmt_prepare(): %s", mysql_error(db));
      return -1;
    }
  }

  memset ((void *) param, 0, sizeof (param));
  param[0].buffer_type = MYSQL_TYPE_DATETIME;
  param[0].buffer = (void *) &my_newend;
  param[0].is_null = 0;

  param[1].buffer_type = MYSQL_TYPE_LONG;
  param[1].buffer = (void *)&ip;
  param[1].is_unsigned = 0;
  param[1].is_null = 0;

  param[2].buffer_type = MYSQL_TYPE_DATETIME;
  param[2].buffer = (void *) &my_thatstart;
  param[2].is_null = 0;

  if (prolong) {
    param[3].buffer_type = MYSQL_TYPE_DATETIME;
    param[3].buffer = (void *) &my_newend;
    param[3].is_null = 0;
    
    param[4].buffer_type = MYSQL_TYPE_DATETIME;
    param[4].buffer = (void *) &my_thatend;
    param[4].is_null = 0;
  } else {
    param[3].buffer_type = MYSQL_TYPE_DATETIME;
    param[3].buffer = (void *) &my_thatend;
    param[3].is_null = 0;
  }

  if (mysql_stmt_bind_param(stmt, param) != 0) {
    syslog(LOG_ERR, "mysql_bind_param(): %s", mysql_error(db));
    return -1;
  }

  if (mysql_stmt_execute(stmt) != 0) {
    syslog(LOG_ERR, "mysql_execute(): %s", mysql_error(db));
    return -1;
  }
 
  mysql_stmt_close(stmt);
  return 0;
}

/* Insert a new lease into the database */
int do_make_lease(MYSQL *db, int ip, time_t start, time_t end, int hw, int cid, int rid) {
  MYSQL_STMT *stmt=NULL;
  MYSQL_BIND param[6];
    
  MYSQL_TIME my_start, my_end;

  timet2mytime(start, &my_start);
  timet2mytime(end, &my_end);

  if ((stmt = mysql_stmt_init(db)) == NULL) {
    syslog(LOG_ERR, "mysql_stmt_init(): %s", mysql_error(db));
    return -1;
  }

  if (mysql_stmt_prepare(stmt, MAKE_LEASE_RSQL, strlen(MAKE_LEASE_RSQL)) != 0) {
    syslog(LOG_ERR, "mysql_stmt_prepare(): %s", mysql_error(db));
    return -1;
  }

  memset ((void *) param, 0, sizeof (param));
  param[0].buffer_type = MYSQL_TYPE_LONG;
  param[0].buffer = (void *)&ip;
  param[0].is_unsigned = 0;
  param[0].is_null = 0;

  param[1].buffer_type = MYSQL_TYPE_DATETIME;
  param[1].buffer = (void *) &my_start;
  param[1].is_null = 0;

  param[2].buffer_type = MYSQL_TYPE_DATETIME;
  param[2].buffer = (void *) &my_end;
  param[2].is_null = 0;

  param[3].buffer_type = MYSQL_TYPE_LONG;
  param[3].buffer = (void *)&hw;
  param[3].is_unsigned = 0;
  param[3].is_null = 0;

  param[4].buffer_type = MYSQL_TYPE_LONG;
  param[4].buffer = (void *)&cid;
  param[4].is_unsigned = 0;
  param[4].is_null = 0;

  param[5].buffer_type = MYSQL_TYPE_LONG;
  param[5].buffer = (void *)&rid;
  param[5].is_unsigned = 0;
  param[5].is_null = 0;

  if (mysql_stmt_bind_param(stmt, param) != 0) {
    syslog(LOG_ERR, "mysql_bind_param(): %s", mysql_error(db));
    return -1;
  }

  if (mysql_stmt_execute(stmt) != 0) {
    syslog(LOG_ERR, "mysql_execute(): %s", mysql_error(db));
    return -1;
  }
 
  mysql_stmt_close(stmt);
  return 0;
}

int writePidFile(char *filename) {
  int result=1;
  FILE *pidfile=fopen(filename,"w");
  char buf[32];
  snprintf(buf, sizeof(buf), "%ld\n", (long) getpid());
  if (fwrite(buf, 1, strlen(buf), pidfile) != strlen(buf)) {
    perror("Writing to PID file");
    result=0;
  }
  fclose(pidfile);
  return result;
}


/* Main program - parse arguments, fork and start running */
int main(int argc, char** argv)
{
  sqlite3 *ldb;
  MYSQL rdb;

  struct sqlite3_stmt* ldb_query;
  int idx=1, r;
  int reset=0;
  int do_fork=1;
  int be_quiet=0;
  int lasttime=0;
  struct stat stbuf;
  my_bool bool_true=1;

  int pid=getpid();
  int syslog_opts=LOG_PID;

  int o;
  char *ldb_filename=NULL;
  char *rdb_host=NULL;
  char *rdb_user=NULL;
  char *rdb_password=NULL;
  char *rdb_db=NULL;
  char *pidfile=NULL;
  int rdb_connected=0;

  while ((o=getopt(argc, argv, "l:h:u:p:d:RFQP:D")) != -1) {
    switch (o) {
    case 'l': ldb_filename = optarg;
      break;
    case 'h': rdb_host = optarg;
      break;
    case 'u': rdb_user = optarg;
      break;
    case 'p': rdb_password = optarg;
      break;
    case 'd': rdb_db = optarg;
      break;
    case 'R': reset = 1;
      break;
    case 'F': do_fork = 0;
      break;
    case 'Q': be_quiet = 1;
      break;
    case 'P': pidfile = optarg;
      break;
    case 'D': gluffdebug = 1;
      break;
    default:
      usage(argv[0]);
      return -1;
      break;
    }
  }

  if (!ldb_filename || !rdb_host || !rdb_user || !rdb_password || !rdb_db) {
    usage(argv[0]);
    return -1;
  }

  if (!be_quiet) syslog_opts |= LOG_PERROR;

  openlog("gluff", syslog_opts, LOG_LOCAL2);

  if (stat(ldb_filename, &stbuf) != 0) {
    syslog(LOG_INFO, "Creating sqlite3 database %s", ldb_filename);
    if (sqlite3_open(ldb_filename, &ldb) == SQLITE_OK) {
      sqlite3_extended_result_codes(ldb, 1);
      sqlite3_busy_timeout(ldb, 600);
      
      if (sqlite3_exec(ldb, "CREATE TABLE IF NOT EXISTS lease_queue (start integer, rtype integer, idx integer, claimed integer, end integer, ip text, hw text, cid text, rid text, primary key(start, idx))", NULL, NULL, NULL) == SQLITE_OK) {
	syslog(LOG_ERR, "Failed to create table lease_queue: %s", sqlite3_errmsg(ldb));
	sqlite3_close(ldb);
	ldb = NULL;
	return -2;
      }
      sqlite3_close(ldb);
    } else {
      syslog(LOG_ERR, "Failed to create database %s: %s", ldb_filename, sqlite3_errmsg(ldb));
      return -2;
    }
  }

    
  if (do_fork) {
    if (!(mysql_init(&rdb))) {
      syslog(LOG_ERR, "mysql_init(): %s", mysql_error(&rdb));
      return -11;
    }

    if (!(mysql_real_connect(&rdb, rdb_host, rdb_user, rdb_password, rdb_db, 0, NULL, 0))) {
      syslog(LOG_ERR, "mysql_real_connect(): %s", mysql_error(&rdb));
      return -12;
    }

    mysql_close(&rdb);

    closelog();

    if (fork()) {
      return 0;
    }

    close(0);
    close(1);
    close(2);

    syslog_opts &= ~LOG_PERROR;
    openlog("gluff", syslog_opts, LOG_LOCAL2); 
  }
  
  if (pidfile)
    if (!writePidFile(pidfile))
      exit(-2);

  if (sqlite3_open(ldb_filename, &ldb) != SQLITE_OK) {
    syslog(LOG_ERR, "Failed to open sqlite3 database %s: %s", ldb_filename, sqlite3_errmsg(ldb));
    return -10;
  }

  sqlite3_extended_result_codes(ldb, 1);
  sqlite3_busy_timeout(ldb, 6000);
      
  if (!(mysql_init(&rdb))) {
    syslog(LOG_ERR, "mysql_init(): %s", mysql_error(&rdb));
    return -11;
  }

  if (!(mysql_real_connect(&rdb, rdb_host, rdb_user, rdb_password, rdb_db, 0, NULL, 0))) {
    syslog(LOG_ERR, "mysql_real_connect(): %s", mysql_error(&rdb));
    return -12;
  }

  rdb_connected=1;

  // For versions before 5.1.6, this has to be called *after* mysql_real_connect(); for
  // versions before 5.0.3, it doesn't have to be called at all, since it's the default.
  // Setting the option here seems like the safest solution.
  mysql_options(&rdb, MYSQL_OPT_RECONNECT, &bool_true);

  syslog(LOG_INFO, "%s v%s starting, using Sqlite3 database %s and MySQL database mysql://%s@%s/%s", PRODUCT, VERSION, ldb_filename, rdb_user, rdb_host, rdb_db);

  /* Resetting means that we change back the 'claimed' column for all records in the queue to "0"
     before we start. This is safe if you are running only one "consumer" on any given sqlite3
     database, i.e. practically always. */
  if (reset && sqlite3_exec(ldb, RESET_LSQL, NULL, NULL, NULL) != SQLITE_OK) {
    syslog(LOG_ERR, "Failed to reset queue entries: %s", sqlite3_errmsg(ldb));
    return -20;
  }
  
  /* Loop forever, first "claiming" any new records by changing the "claimed" column to our own PID,
     then reading them one at a time in chronological order, and updating the MySQL database. 
     If something fails along the way, generally an error will be logged and the application exits.
  */
  while(1) {
    if (!mysql_ping(&rdb)) {
      int now=time(NULL);

      if (!rdb_connected) {
	syslog(LOG_INFO, "Re-connected to MySQL server");
	rdb_connected=1;
      }
      
      if (now != lasttime) {
	lasttime = now;
	idx=1;
      }

      if (sqlite3_prepare_v2(ldb, CLAIM_LSQL, strlen(CLAIM_LSQL), &ldb_query, NULL) != SQLITE_OK ||
	  sqlite3_bind_int(ldb_query,1,pid) != SQLITE_OK) {
	syslog(LOG_ERR, "Failed to claim queue entries: %s", sqlite3_errmsg(ldb));
	return -20;
      }

      if (sqlite3_step(ldb_query) != SQLITE_DONE) {
	syslog(LOG_ERR, "sqlite3_step(): %s", sqlite3_errmsg(ldb));
      }
      
      if (sqlite3_finalize(ldb_query) != SQLITE_OK) {
	syslog(LOG_ERR, "sqlite3_finalize(): %s", sqlite3_errmsg(ldb));
      }
      
      if (sqlite3_prepare_v2(ldb, GET_LSQL, strlen(GET_LSQL), &ldb_query, NULL) != SQLITE_OK ||
	  sqlite3_bind_int(ldb_query,1,pid) != SQLITE_OK) {
	syslog(LOG_ERR, "Failed to retrieve queue entries: %s", sqlite3_errmsg(ldb));
	return -20;
      }

      while ((r=sqlite3_step(ldb_query)) == SQLITE_BUSY || (r == SQLITE_ROW)) {
	if (r == SQLITE_BUSY) usleep(300000);
	else {
	  const unsigned char *cidstr;
	  const unsigned char *ridstr;
	  int cid, rid;
	  // start, end, ip, hw, cid, rid
	  time_t start = sqlite3_column_int(ldb_query, 0);
	  int rtype = sqlite3_column_int(ldb_query, 1);
	  time_t end = sqlite3_column_int(ldb_query, 2);
	  const unsigned char *ipstr = sqlite3_column_text(ldb_query, 3);
	  const unsigned char *hwstr = sqlite3_column_text(ldb_query, 4);
	  if (sqlite3_column_type(ldb_query, 5) != SQLITE_NULL) {
	    cidstr = sqlite3_column_text(ldb_query, 5);
	    cid = rdb_cid_id(&rdb,cidstr);
	  } else {
	    cidstr = (unsigned char *)"<NULL>";
	    cid = 0;
	  }
	  if (sqlite3_column_type(ldb_query, 6) != SQLITE_NULL) {
	    ridstr = sqlite3_column_text(ldb_query, 6);
	    rid = rdb_rid_id(&rdb,ridstr);
	  } else {
	    ridstr = (unsigned char *)"<NULL>";
	    rid = 0;
	  }
	  int ip = rdb_ip_id(&rdb,ipstr);
	  int hw = rdb_hw_id(&rdb,hwstr);
	  time_t thatstart, thatend;
	  int thathw=-1, thatcid=-1, thatrid=-1;
	  
	  if (!(ip * hw)) return -15;

	  char tbuf1[64], tbuf2[64];
	  
	  ctime_r(&start, tbuf1);
	  ctime_r(&end, tbuf2);

	  if (tbuf1[strlen(tbuf1) - 1] == '\n') tbuf1[strlen(tbuf1) - 1] = '\0';
	  if (tbuf2[strlen(tbuf2) - 1] == '\n') tbuf2[strlen(tbuf2) - 1] = '\0';

	  syslog(LOG_DEBUG, "%s on ip: %s, hw: %s, cid: %s, rid: %s, start: %s, end: %s",
		 (rtype==1)?"RELEASE":"ACK",
		 ipstr, hwstr, cidstr, ridstr, tbuf1, tbuf2);

	  int makelease=1;
	  if ((r=do_find_lease(&rdb, ip, start, &thatstart, &thatend, &thathw, &thatcid, &thatrid)) > 0) {
	    if (gluffdebug) {
	      syslog(LOG_DEBUG, "Found lease in rdb. hw(%d,%d), cid(%d,%d), rid(%d,%d)", hw, thathw, cid, thatcid, rid, thatrid);
	    }
	    if (hw != thathw || cid != thatcid || rid != thatrid) {
	      if (gluffdebug) {
		syslog(LOG_DEBUG, "Different hw, cid or rid. Cutting off and making a new one");
	      }
	      do_update_lease(&rdb, ip, thatstart, thatend, start, 0); // cut off old lease
	    } else {
	      if (rtype == 1) {
		if (gluffdebug) {
		  syslog(LOG_DEBUG, "Release. Cutting off the lease I found");
		}
		do_update_lease(&rdb, ip, thatstart, thatend, end, 0); // cut off old lease
	      } else {
		if (gluffdebug) {
		  syslog(LOG_DEBUG, "Prolonging identical lease");
		}
		do_update_lease(&rdb, ip, thatstart, thatend, end, 1); // prolong lease
	      }
	      makelease=0;
	    }
	  } else if (r<0) {
	    syslog(LOG_ERR, "do_find_lease(): %s", mysql_error(&rdb));
	    return -16;
	  }
	  if (makelease) {
	    if (gluffdebug) {
	      syslog(LOG_DEBUG, "Making new lease entry");
	    }
	    if (do_make_lease(&rdb, ip, start, end, hw, cid, rid) != 0) return -17;
	  }
	}
      }
      
      if (r != SQLITE_DONE) {
	syslog(LOG_ERR, "sqlite3_step(): %s", sqlite3_errmsg(ldb));
      }
      
      if (sqlite3_finalize(ldb_query) != SQLITE_OK) {
	syslog(LOG_ERR, "sqlite3_finalize(): %s", sqlite3_errmsg(ldb));
      }
      
      if (sqlite3_prepare_v2(ldb, CLEAR_LSQL, strlen(CLEAR_LSQL), &ldb_query, NULL) != SQLITE_OK ||
	  sqlite3_bind_int(ldb_query,1,pid) != SQLITE_OK) {
	syslog(LOG_ERR, "Failed to clear queue entries: %s", sqlite3_errmsg(ldb));
	return -20;
      }
      while ((r=sqlite3_step(ldb_query)) == SQLITE_BUSY) {
	usleep(1000);
      }
      
      if (r != SQLITE_DONE) {
	syslog(LOG_ERR, "sqlite3_step(): %s", sqlite3_errmsg(ldb));
      }
      
      if (sqlite3_finalize(ldb_query) != SQLITE_OK) {
	syslog(LOG_ERR, "sqlite3_finalize(): %s", sqlite3_errmsg(ldb));
      }
    } else {
      if (rdb_connected) {
	syslog(LOG_WARNING, "MySQL server unreachable");
	rdb_connected=0;
      }
    }
    sleep(5);
  }

  return 1;
} 
