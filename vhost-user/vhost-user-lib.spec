Name: vhost-user-lib
Version: 1.0
Release: r1
Summary: lib for vhost user protocol
License: BSD-3-Clause
Group: vhost-user-lib
Source0: %{name}-%{version}.tar.gz

BuildRequires: cmake libstd
Requires: libstd

%description
Vhost user library which provide APIs to process the vhost user message.

%package devel
Summary: Headers
Group: vhost-user-lib
License: BSD-3-Clause
Requires: %{name} = %{version}-%{release}


%description devel
Vhost user library which provide APIs to process the vhost user message.

%prep
%setup -qn %{name}-%{version}

%build
%cmake

%cmake_build

%install
%cmake_install

%files
%{_usr}/lib/libvhostuserlib.so

%files devel
%{_includedir}/*

