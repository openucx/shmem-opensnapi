/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#ifndef _SHMEMIO_TEST_UTIL_H
#define _SHMEMIO_TEST_UTIL_H

#include <ucs/config/global_opts.h>

#define CCAT(_a_,_b_)_a_##_b_
#define CAT(_a_,_b_) CCAT(_a_,_b_)

#define SSTR(_str_) #_str_
#define STR(_str_) SSTR(_str_)

#define logtag(_lvl_) STR(_lvl_)":"__FILE__":"STR(__LINE__)": "

#ifdef ENABLE_DEBUG

typedef enum {
  shmemio_log_level_error = 1,
  shmemio_log_level_warn = 2,
  shmemio_log_level_info = 3,
  shmemio_log_level_trace = 4,
} shmemio_log_level_t;

extern int shmemio_log_level;

#define shmemio_set_log_level(_lvl_) {				\
    shmemio_log_level = CCAT(shmemio_log_level_,_lvl_); }
//printf ("%s:%d: Setting log level to %s\n", __FILE__, __LINE__, STR(_lvl_)); 


#define shmemio_do_error(...) if (shmemio_log_level >= shmemio_log_level_error) { __VA_ARGS__; }
#define shmemio_do_warn(...) if (shmemio_log_level >= shmemio_log_level_warn) { __VA_ARGS__; }
#define shmemio_do_info(...) if (shmemio_log_level >= shmemio_log_level_info) { __VA_ARGS__; }
#define shmemio_do_trace(...) if (shmemio_log_level >= shmemio_log_level_trace) { __VA_ARGS__; }

// TODO: figure out why shmemu assert is crashing and replace this
#define shmemio_assert(_test_,...) { if (!(_test_)) { printf(logtag()__VA_ARGS__); fflush(stdout); do {;} while(1); } }

#define shmemio_init_check() shmemio_assert((proc.io.nfpes >= 0), "shmemio not initialized when expected");

#define shmemio_pe_range_check(_pe_) \
  if (((_pe_) < proc.nranks) || (((_pe_) - proc.nranks > proc.io.nfpes))) \
    { printf ("fpe %d out of range [%d:%d] at %s:%d\n", (_pe_),		\
	      proc.nranks, proc.nranks + proc.io.nfpes, __FILE__, __LINE__); \
      fflush(stdout); exit(-1); }

#define shmemio_fspace_range_check(_fid_) \
  if (((_fid_) < 0) || ((_fid_) > proc.io.nfspaces)) \
    { printf ("fspace %d out of range [:%d] at %s:%d\n", (_fid_), \
	      proc.io.nfspaces, __FILE__, __LINE__); fflush(stdout), exit(-1); }

#define shmemio_valid_check(_fp_) \
  if ( ((_fp_) == NULL) || ((_fp_)->valid != 1) ) {	\
  printf ("Invalid fpe or fspace pointer %p at %s:%d\n", \
	  (_fp_), __FILE__, __LINE__); }

#else

#define shmemio_set_log_level(_lvl_)

#define shmemio_assert(_test_,...)
#define shmemio_pe_range_check(_pe_)
#define shmemio_fspace_range_check(_fid_)
#define shmemio_valid_check(_fp_)

#define shmemio_do_error(...) __VA_ARGS__
#define shmemio_do_warn(...)
#define shmemio_do_info(...)
#define shmemio_do_trace(...)

#endif

#define VA_CLEAN(...) , ##__VA_ARGS__

#ifdef _THISPE_H
#define MYRANK proc.rank
#else
//#define MYRANK (pthread_self())
#define MYRANK -1
#endif

#define shmemio_msg(_lvl_,_fmt_, ...)	\
  fprintf (stderr, "[%d]"logtag(_lvl_)_fmt_, MYRANK VA_CLEAN(__VA_ARGS__))

#define shmemio_log(_lvl_, ...)			\
  CAT(shmemio_do_,_lvl_)({ shmemio_msg(_lvl_, __VA_ARGS__); })

#define shmemio_log_if(_lvl_, _test_, ...)			\
  CAT(shmemio_do_,_lvl_)({					\
      if (_test_) { shmemio_msg(_lvl_, __VA_ARGS__); } })

#define shmemio_log_jmp_if(_lvl_, _jmp_, _test_, ...)			\
  CAT(shmemio_do_,_lvl_)({						\
      if (_test_) { shmemio_msg(_lvl_, __VA_ARGS__); goto _jmp_; } })

#define shmemio_log_ret_if(_lvl_, _ret_, _test_, ...)			\
  CAT(shmemio_do_,_lvl_)({						\
      if (_test_) { shmemio_msg(_lvl_, __VA_ARGS__); return _ret_; } })

#endif

/*********** Debugging functions ****************/

static inline void
modify_config(const char *name, const char *val, int optional)
{
  ucs_status_t status = ucs_global_opts_set_value(name, val);
  
  if ((status == UCS_OK) || (optional && (status == UCS_ERR_NO_ELEM))) {
    return;
  }

  printf ("Invalid config option: %s\n");
}

static inline void
debug_log(int on)
{
  //modify_config("LOG_FILE", "file:debug.log", 0);

  if (on) {
    //modify_config("LOG_LEVEL", "debug", 0);
    modify_config("LOG_LEVEL", "trace", 0);
  }
  else {
    modify_config("LOG_LEVEL", "warn", 0);
  }
}

static inline void
print_bytes(char *buf, size_t len)
{
  size_t i;
  for (i = 0; i < len; i++)
    {
      if ((i & 0xF) == 0) {
	printf ("\n");
      }
      else {
	printf(":");
      }
      printf("%02X", buf[i]);
    }
}


static inline char *
time2str(time_t *t)
{
  struct tm *p = localtime(t);
  static char s[1024];		
  strftime(s, 1024, "%Y,%b,%d,%H:%M:%S", p);
  return s;
}

#define shmemio_log_sfile(_lvl_, _sfile_, _msg_) {			\
  shmemio_log(_lvl_,							\
	      "%s, sfile[%s]: size=%lu, offset=%lu, openc=%d, waitc=%d, ctime[%s], atime[%s], mtime[%s], ftime[%s]\n", \
	      _msg_, (_sfile_).sfile_key, (_sfile_).size, (_sfile_).offset, (_sfile_).open_count, (_sfile_).close_waitc, \
	      time2str(&((_sfile_).ctime)), time2str(&((_sfile_).atime)), \
	      time2str(&((_sfile_).mtime)), time2str(&((_sfile_).ftime))); }

#define shmemio_log_fp(_lvl_, _fp_, _msg_) {					\
    shmemio_log(_lvl_,							\
		"%s, fp %p: addr=%p, fkey=%lu, size=%lu, unit=%d, pes=[%d:%d] by %d, offset=%lu, l_region=%d, fspace=%d\n", \
		_msg_, _fp_, _fp_->addr, _fp_->fkey, _fp_->size, _fp_->unit_size, _fp_->pe_start, \
		_fp_->pe_start + _fp_->pe_size - 1,			\
		_fp_->pe_stride, (unsigned)_fp_->offset, _fp_->l_region, _fp_->fspace); }

