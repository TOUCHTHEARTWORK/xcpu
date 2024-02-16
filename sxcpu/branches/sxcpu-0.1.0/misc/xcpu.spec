# Philip Soltero; psoltero@cs.unm.edu
# Scalable Systems Lab
# Computer Science Department
# University of New Mexico
#
# Packages the XCPU cluster management software developed by the Los Alamos
# National Laboratories.

Name:       xcpu
Version:    1.0
Release:    1
Group:      Applications/System
Vendor:     Los Alamos National Laboratories
URL:        http://www.xcpu.org
Packager:   Philip Soltero <psoltero@cs.unm.edu>
License:    MIT/GPL
Source:     %{name}-%{version}.tar.gz
Summary:    Provides a means to remotely execute software on a cluster.
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

%prep
rm -rf $RPM_BUILD_ROOT

%setup -q

%build
make

%install
mkdir $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/etc/init.d
cp misc/xcpufs.sh $RPM_BUILD_ROOT/etc/init.d/xcpufs
make install INSTALLPREFIX=$RPM_BUILD_ROOT/usr
make installman INSTALLPREFIX=$RPM_BUILD_ROOT/usr

%clean
rm -rf $RPM_BUILD_ROOT

%post
chkconfig --add xcpufs

%preun
chkconfig --del xcpufs

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
/usr/include/libxauth.h
/usr/include/libxcpu.h
/usr/include/spclient.h
/usr/include/spfs.h
/usr/include/strutil.h
/usr/include/xcpu.h
/usr/lib/libspclient.a
/usr/lib/libspfs.a
/usr/lib/libstrutil.a
/usr/lib/libxauth.a
/usr/lib/libxcpu.a
/usr/share/man/man1/xk.1.gz
/usr/share/man/man1/xps.1.gz
/usr/share/man/man1/xrx.1.gz
/usr/share/man/man1/xstat.1.gz
/usr/share/man/man4/statfs.4.gz

%changelog
* Tue Jun 26 2007 Philip Soltero <psoltero@cs.unm.edu>
- Initial build.

