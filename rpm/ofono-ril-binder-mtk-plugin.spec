Name: ofono-ril-binder-mtk-plugin
Version: 0
Release: 1
Summary: Ofono MediaTek RIL binder plugin
Group: Development/Libraries
License: BSD
URL: https://github.com/mer-hybris/ofono-ril-binder-plugin
Source: %{name}-%{version}.tar.bz2

Requires: ofono >= 1.21+git42
Requires: libgrilio >= 1.0.27
Requires: libgbinder >= 1.0.30
Requires: libgbinder-radio >= 1.0.3
BuildRequires: ofono-devel >= 1.21+git42
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(libgrilio) >= 1.0.27
BuildRequires: pkgconfig(libgrilio-binder)
BuildRequires: pkgconfig(libgbinder-radio) >= 1.0.3
BuildRequires: pkgconfig(libgbinder) >= 1.0.30

%define plugin_dir %{_libdir}/ofono/plugins

%description
This package contains ofono plugin which implements binder transport for RIL

%prep
%setup -q -n %{name}-%{version}

%build
make %{_smp_mflags} KEEP_SYMBOLS=1 release

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}

mkdir -p %{buildroot}/%{plugin_dir}
%preun

%files
%dir %{plugin_dir}
%defattr(-,root,root,-)
%{plugin_dir}/rilbinderplugin-mtk.so
