Name: vhost-user-test
Version: 1.0
Release: r1
Summary: test cases for vhost user lib
License: BSD-3-Clause-Clear
Source0: %{name}-%{version}.tar.gz

BuildRequires: make libstd vhost-user-lib
Requires: libstd vhost-user-lib

%description
Provide a way to boot a vhost user library based vhost user server and vhost user client.
By send and receive vhost user message between server and client, test the vhost user library's APIs.

%prep
%setup -qn %{name}-%{version}

%build
%cmake

%cmake_build

%install
%cmake_install

%files
%{_bindir}/vhost-user-test

