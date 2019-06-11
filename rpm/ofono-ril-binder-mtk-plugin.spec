Name: ofono-ril-binder-mtk-plugin
Version: 0
Release: 1
Summary: Ofono MediaTek RIL binder plugin
Group: Development/Libraries
License: BSD
URL: https://github.com/mer-hybris/ofono-ril-binder-plugin
Source: %{name}-%{version}.tar.bz2

%define ofono_version 1.21+git50
%define libgrilio_version 1.0.32
%define libgrilio_binder_version 1.0.7
%define libgbinder_version 1.0.30
%define libgbinder_radio_version 1.0.6

Requires: ofono >=  %{ofono_version}
Requires: libgrilio >= %{libgrilio_version}
Requires: libgbinder >= %{libgbinder_version}
Requires: libgbinder-radio >= %{libgbinder_radio_version}
BuildRequires: ofono-devel >=  %{ofono_version}
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(libgrilio) >= %{libgrilio_version}
BuildRequires: pkgconfig(libgrilio-binder) >= %{libgrilio_binder_version}
BuildRequires: pkgconfig(libgbinder-radio) >= %{libgbinder_radio_version}
BuildRequires: pkgconfig(libgbinder) >= %{libgbinder_version}

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
