/* Copyright (C) 2013-2015 Codership Oy <info@codership.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA. */

#ifndef WSREP_UTILS_H
#define WSREP_UTILS_H

#include "wsrep_priv.h"

unsigned int wsrep_check_ip(const char *addr);
size_t wsrep_guess_ip(char *buf, size_t buf_len);

/* returns the length of the host part of the address string */
size_t wsrep_host_len(const char *addr, size_t addr_len);

namespace wsp {
class node_status {
 public:
  node_status() : status(WSREP_MEMBER_UNDEFINED) {}
  void set(wsrep_member_status_t new_status,
           const wsrep_view_info_t *view = 0) {
    if (status != new_status || 0 != view) {
      wsrep_notify_status(new_status, view);
      status = new_status;
    }
  }
  wsrep_member_status_t get() const { return status; }

 private:
  wsrep_member_status_t status;
};
} /* namespace wsp */

extern wsp::node_status local_status;

namespace wsp {
/* a class to manage env vars array */
class env {
 private:
  size_t len_;
  char **env_;
  int errno_;
  bool ctor_common(char **e);
  void dtor();
  env &operator=(env);

 public:
  explicit env(char **env);
  explicit env(const env &);
  ~env();
  int append(const char *var); /* add a new env. var */
  int error() const { return errno_; }
  char **operator()() { return env_; }
};

/* A small class to run external programs. */
class process {
 private:
  const char *const str_;
  FILE *io_;
  FILE *io_w_;
  int err_;
  pid_t pid_;

 public:
  /*! @arg type is a pointer to a null-terminated string which must contain
           either the letter 'r' for reading, or the letter 'w' for writing,
           or the letters 'rw' for both reading and writing.
      @arg env optional null-terminated vector of environment variables
      @arg execute_immediately  If this is set to true, then the command will
           be executed while in the constructor.
           Executing the command from the constructor caused problems
           due to dealing with errors, so the ability to execute the
           command separately was added.
   */
  process(const char *cmd, const char *type, char **env, bool execute_immediately=true);
  ~process();

  /* If type is 'r' or 'rw' this is the read pipe
     Else if type is 'w', this is the write pipe
  */
  FILE *pipe() { return io_; }

  /* If type is 'rw' this is the write pipe
     Else if type is 'r' or 'w' this is NULL
     This variable is only set if the type is 'rw'
  */
  FILE* write_pipe() { return io_w_; }

  /* Closes the write pipe so that the other side will get an EOF
     (and not hang while waiting for the rest of the data).
  */
  void close_write_pipe();

  void execute(const char *type, char **env);

  int error() { return err_; }
  int wait();
  const char *cmd() { return str_; }
  void terminate();
};

class thd {
  class thd_init {
   public:
    thd_init() { my_thread_init(); }
    ~thd_init() { my_thread_end(); }
  } init;

  thd(const thd &);
  thd &operator=(const thd &);

 public:
  thd(bool wsrep_on);
  ~thd();
  THD *ptr;
};

class string {
 public:
  string() : string_(0) {}
  explicit string(size_t s) : string_(static_cast<char *>(malloc(s))) {}
  char *operator()() { return string_; }
  void set(char *str) {
    if (string_) free(string_);
    string_ = str;
  }
  ~string() { set(0); }

 private:
  char *string_;
};

#ifdef REMOVED
class lock {
  pthread_mutex_t *const mtx_;

 public:
  lock(pthread_mutex_t *mtx) : mtx_(mtx) {
    int err = pthread_mutex_lock(mtx_);

    if (err) {
      WSREP_ERROR("Mutex lock failed: %s", strerror(err));
      abort();
    }
  }

  virtual ~lock() {
    int err = pthread_mutex_unlock(mtx_);

    if (err) {
      WSREP_ERROR("Mutex unlock failed: %s", strerror(err));
      abort();
    }
  }

  inline void wait(pthread_cond_t *cond) { pthread_cond_wait(cond, mtx_); }

 private:
  lock(const lock &);
  lock &operator=(const lock &);
};

class monitor {
  int mutable refcnt;
  pthread_mutex_t mutable mtx;
  pthread_cond_t mutable cond;

 public:
  monitor() : refcnt(0) {
    pthread_mutex_init(&mtx, NULL);
    pthread_cond_init(&cond, NULL);
  }

  ~monitor() {
    pthread_mutex_destroy(&mtx);
    pthread_cond_destroy(&cond);
  }

  void enter() const {
    lock l(&mtx);

    while (refcnt) {
      l.wait(&cond);
    }
    refcnt++;
  }

  void leave() const {
    lock l(&mtx);

    refcnt--;
    if (refcnt == 0) {
      pthread_cond_signal(&cond);
    }
  }

 private:
  monitor(const monitor &);
  monitor &operator=(const monitor &);
};

class critical {
  const monitor &mon;

 public:
  critical(const monitor &m) : mon(m) { mon.enter(); }

  ~critical() { mon.leave(); }

 private:
  critical(const critical &);
  critical &operator=(const critical &);
};
#endif


class WSREPState
{
  public:
    /* Resets all of the data to default values */
    void clear() { wsrep_schema_version.clear(); }

    bool load_from(const char *dir, const char *filename);
    bool save_to(const char *dir, const char *filename);

    /* Compare the server version with the wsrep version
       Returns true if the wsrep version matches the server version EXACTLY
       (to the major.minor.revision values).
    */
    bool wsrep_schema_version_equals(const char *server_version);

    /* Before saving and after loading, the version string
       may be modified/truncated to conform to "x.y.z"
       e.g. "8.0.15-5" would be shortened to "8.0.15"
       and "8.0" would be lengthened to "8.0.0"
    */
    std::string wsrep_schema_version;

  private:
    /* Parses a "a.b.c" version string into it's three component
       parts.  If a compoenent is missing, it will be assinged
       a value of 0.
    */
    void parse_version(const char *str, uint &major, uint &minor, uint &revision);
};

}  // namespace wsp

#endif /* WSREP_UTILS_H */
