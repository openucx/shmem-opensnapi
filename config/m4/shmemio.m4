# Copyright (c) 2018 Arm, Ltd
# Option to compile with/without shmem io api
AC_ARG_ENABLE(shmemio,
  [ --disable-shmemio   Disable compilation of shmemio api],
  [case "${enableval}" in
     yes | no ) enable_shmemio="${enableval}" ;;
     *) AC_MSG_ERROR(bad value ${enableval} for --disable-shmemio) ;;
   esac],
  [enable_shmemio="yes"]
)

AS_IF([test "x$enable_shmemio" != "xno"],
        [
	  INC_API_SHMEMIO='#define INCLUDE_API_SHMEMIO'
	  AC_DEFINE([ENABLE_SHMEMIO], [1], [Enable shmem io api support])
          AC_SUBST([ENABLE_SHMEMIO], [1])
	  AC_SUBST([SHMEMIO_LIBS], [-lshmemio])
	  AC_SUBST([INC_API_SHMEMIO])
	],
        [
	  AC_SUBST([ENABLE_SHMEMIO], [0])
	  AC_SUBST([SHMEMIO_LIBS], [])
	  AC_SUBST([INC_API_SHMEMIO], [])
	]
        )

AM_CONDITIONAL([ENABLE_SHMEMIO], [test "x$enable_shmemio" != "xno"])
