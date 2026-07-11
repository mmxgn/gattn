Name:           gattn
Version:        0.1.5
Release:        1%{?dist}
Summary:        Attention hub for GNOME

License:        GPL-3.0-or-later
URL:            https://github.com/mmxgn/gattn
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  meson
BuildRequires:  ninja-build
BuildRequires:  gcc
BuildRequires:  pkgconfig(gtk4)
BuildRequires:  pkgconfig(libadwaita-1)
BuildRequires:  pkgconfig(vte-2.91-gtk4)
BuildRequires:  pkgconfig(gtksourceview-5)

Requires:       gtk4
Requires:       libadwaita

%description
An attention hub for GNOME inspired by attn. Aggregates multiple
terminal sessions with color-coded state indicators and a grid view.

%prep
%autosetup

%build
%meson
%meson_build

%install
%meson_install

%files
%{_bindir}/gattn
%{_datadir}/applications/org.mmxgn.gattn.desktop
%{_datadir}/icons/hicolor/scalable/apps/org.mmxgn.gattn.svg
