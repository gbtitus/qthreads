dnl -*- autoconf -*-
dnl
dnl Copyright (c) 2008 Sandia Corporation

m4_include([config/ax_create_stdint_h.m4])
m4_include([config/ac_compile_check_sizeof.m4])
dnl Only include this if we're using an old Autoconf.  Remove when we
dnl finally drop support for AC 2.59
m4_if(m4_version_compare(m4_defn([m4_PACKAGE_VERSION]), [2.60]), -1,
      [m4_include([config/ac_prog_cc_c99.m4])
       m4_include([config/ac_use_system_extensions.m4])])

m4_include([config/qthread_check_makecontext.m4])
m4_include([config/qthread_check_sst.m4])
m4_include([config/qthread_check_attribute_aligned.m4])
m4_include([config/qthread_check_altix_timer.m4])
m4_include([config/qthread_check_assembly.m4])
