Name:       unblank-restart-sensors
Summary:    App that restarts sensord service after display wakes up
Version:    0
Release:    1
Group:      System/Applications
License:    TBD
URL:        https://git.jollamobile.com/sffe/unblank-restart-sensors
Source0:    %{name}-%{version}.tar.bz2
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

%files
%defattr(-,root,root,-)
%{_bindir}/unblank-restart-sensors

