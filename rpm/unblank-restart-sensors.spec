Name:       unblank-restart-sensors
Summary:    Hacky service that restarts sensorfwd after display wakes up
Version:    0
Release:    1
Group:      System/Applications
License:    BSD-3
URL:        https://git.jollamobile.com/sffe/unblank-restart-sensors
Source0:    %{name}-%{version}.tar.bz2
Source1:    unblank-restart-sensors.service
Requires:   systemd
Requires(preun): systemd
Requires(post): /sbin/ldconfig
Requires(post): systemd
Requires(postun): /sbin/ldconfig
Requires(postun): systemd
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(dbus-glib-1)
BuildRequires:  pkgconfig(mce)

%description
On mako (Nexus 4) sensors.qcom does not adhere to coherent API sensorfw expects,
in ways that are difficult to debug. This causes sensors to stop working after
blanking the display.
This service restarts sensors whenever display wakes up.
It is an acceptable workaround/hack, due to the fact that mako is not actively
supported by Jolla anymore, with hopes that community will port mako to cm-11.0
and sensors.qcom problem would then go away.
Repediately restarting sensors on unblank has not caused mce to malfunction and
is a compromise between functionality and hack, to release to early adopters.

%prep
%setup -q -n %{name}-%{version}

%build

%qmake5

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%qmake5_install

install -D -m644 %{SOURCE1} $RPM_BUILD_ROOT/%{_lib}/systemd/system/unblank-restart-sensors.service

mkdir -p %{buildroot}/%{_lib}/systemd/system/basic.target.wants
ln -s ../unblank-restart-sensors.service %{buildroot}/%{_lib}/systemd/system/basic.target.wants/unblank-restart-sensors.service

%preun
if [ "$1" -eq 0 ]; then
systemctl stop unblank-restart-sensors.service || :
fi

%post
/sbin/ldconfig
systemctl daemon-reload || :
systemctl reload-or-try-restart unblank-restart-sensors.service || :

%postun
/sbin/ldconfig
systemctl daemon-reload || :

%files
%defattr(-,root,root,-)
%{_bindir}/unblank-restart-sensors
/%{_lib}/systemd/system/unblank-restart-sensors.service
/%{_lib}/systemd/system/basic.target.wants/unblank-restart-sensors.service

