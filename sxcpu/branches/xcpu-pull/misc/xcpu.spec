# Philip Soltero; psoltero@cs.unm.edu
# Scalable Systems Lab
# Computer Science Department
# University of New Mexico
#
# Packages the XCPU cluster management software developed by the Los Alamos
# National Laboratories.

Name:       xcpu
Summary:    Provides a means to remotely execute software on a cluster.
Version:    1.1
Release:    s1
Group:      Applications/System
Vendor:     Los Alamos National Laboratories
URL:        http://www.xcpu.org/dl/xcpu-1.1
Packager:   Philip Soltero <psoltero@cs.unm.edu>
License:    MIT/GPL
Source0:    %{name}-%{version}.tar.gz
BuildRoot:  %{_tmppath}/%{name}-%{version}

%description
Xcpu is a suite for accessing resources, executing jobs and managing nodes
in a cluster configuration. Xcpu contains a set of servers running on remote 
nodes or a head node and a set of client programs and libraries which can be 
used to communicate with the servers or write applications that access them.

%package server
Summary: XCPU server that needs to be installed on every node
Group: Applications/System

%description server
Xcpu is a suite for accessing resources, executing jobs and managing nodes
in a cluster configuration. Xcpu contains a set of servers running on remote 
nodes or a head node and a set of client programs and libraries which can be 
used to communicate with the servers or write applications that access them.

You should install xcpu-server on every node.

%package devel
Summary: Libraries for developing tools that talk to the xcpu server
Group: Development/Libraries

%description devel
Xcpu is a suite for accessing resources, executing jobs and managing nodes
in a cluster configuration. Xcpu contains a set of servers running on remote 
nodes or a head node and a set of client programs and libraries which can be 
used to communicate with the servers or write applications that access them.

If you want to write your own utilities that talk to the xcpu server, you
need to install this package.

%package utils
Summary: XCPU head node utilities
Group: Applications/System

%description utils
Xcpu is a suite for accessing resources, executing jobs and managing nodes
in a cluster configuration. Xcpu contains a set of servers running on remote 
nodes or a head node and a set of client programs and libraries which can be 
used to communicate with the servers or write applications that access them.

This package contains the standard xcpu utilities

%package xbootfs
Summary: xbootfs file transfer server (stand-alone)
Group: Applications/System

%description xbootfs
This package provides a stand-alone boot server.  When the xbootfs server is 
started, it will check for xbootfs config files in /etc/sysconfig/xbootfs and
start a server for each file being served.  This can be installed on master
and client nodes.

%prep
umask 022

%setup -q

%build
umask 022
%{__make}

%install
umask 022
%{__rm} -rf %{buildroot}
%{__mkdir} $RPM_BUILD_ROOT
%{__make} install INSTALLPREFIX=$RPM_BUILD_ROOT/usr
%{__make} installman INSTALLPREFIX=$RPM_BUILD_ROOT/usr
%{__make} installscripts INSTALLPREFIX=$RPM_BUILD_ROOT

%clean
%{__rm} -rf %{buildroot}

%post server
chkconfig --add xcpufs

%preun server
chkconfig --del xcpufs

%post xbootfs
chkconfig --add xbootfs

%preun xbootfs
chkconfig --del xbootfs

%files server
%defattr(0444,root,root)
%attr(0755,root,root) /etc/init.d/xcpufs
%attr(0555,root,root) /usr/sbin/xcpufs
/usr/share/man/man4/xcpufs.4.gz

%files devel
%defattr(0444,root,root)
/usr/include/libxauth.h
/usr/include/libxcpu.h
/usr/include/spclient.h
/usr/include/spfs.h
/usr/include/strutil.h
/usr/include/xcpu.h
/usr/lib/libspclient.a
/usr/lib/libspfs.a
/usr/lib/libstrutil.a
/usr/lib/libxcpu.a
/usr/lib/libxauth.a
/usr/share/man/man4/xcpu.4.gz

%files utils
%defattr(0444,root,root)
%attr(0555,root,root) /usr/bin/statfs
%attr(0555,root,root) /usr/bin/xk
%attr(0555,root,root) /usr/bin/xps
%attr(0555,root,root) /usr/bin/xrx
%attr(0555,root,root) /usr/bin/xstat
%attr(0555,root,root) /usr/bin/xgroupset
%attr(0555,root,root) /usr/bin/xuserset
/usr/share/man/man1/xk.1.gz
/usr/share/man/man1/xps.1.gz
/usr/share/man/man1/xrx.1.gz
/usr/share/man/man1/xstat.1.gz
/usr/share/man/man1/xgroupset.1.gz
/usr/share/man/man1/xuserset.1.gz
/usr/share/man/man4/statfs.4.gz

%files xbootfs
%defattr(0444,root,root)
%attr(0555,root,root) /usr/sbin/xbootfs
%attr(0755,root,root) /etc/init.d/xbootfs
/usr/share/man/man1/xbootfs.1.gz

%changelog
* Fri Mar 21 2008 Kevin Tegtmeier <kevint@lanl.gov>
- Cleaned up files manifest, using more rpm macros

* Thu Mar 20 2008 Kevin Tegtmeier <kevint@lanl.gov>
- Added xbootfs, cleaned up specfile for newest patches

* Tue Jun 26 2007 Philip Soltero <psoltero@cs.unm.edu>
- Initial build.

