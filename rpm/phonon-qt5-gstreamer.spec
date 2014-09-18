# 
# Do NOT Edit the Auto-generated Part!
# Generated by: spectacle version 0.27
# 

Name:       phonon-qt5-gstreamer

# >> macros
# << macros

# >> bcond_with
# << bcond_with

# >> bcond_without
# << bcond_without

Summary:    Multimedia framework api
Version:    4.8.0
Release:    1
Group:      System/Base
License:    GPLv2+
URL:        http://www.kde.org
Source0:    %{name}-%{version}.tar.xz
Source100:  phonon-qt5-gstreamer.yaml
Source101:  phonon-qt5-gstreamer-rpmlintrc
Requires:   kf5-filesystem
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Xml)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Widgets)
BuildRequires:  pkgconfig(Qt5Concurrent)
BuildRequires:  pkgconfig(Qt5Test)
BuildRequires:  pkgconfig(Qt5OpenGL)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(libxml-2.0)
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(gstreamer-plugins-base-1.0)
BuildRequires:  cmake
BuildRequires:  kf5-rpm-macros
BuildRequires:  qt5-tools
BuildRequires:  phonon-qt5-devel

%description
Multimedia framework api


%prep
%setup -q -n %{name}-%{version}/upstream

# >> setup
# << setup

%build
# >> build pre
mkdir -p %{_target_platform}-Qt5
pushd %{_target_platform}-Qt5
%{cmake} \
-DPHONON_BUILD_PHONON4QT5:BOOL=ON \
-DBUILD_SHARED_LIBS:BOOL=ON \
-DBUILD_TESTING:BOOL=FALSE \
-DCMAKE_BUILD_TYPE=%{_kf5_build_type} \
-DCMAKE_INSTALL_PREFIX:PATH=%{_kf5_prefix} \
-DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
-DBIN_INSTALL_DIR:PATH=%{_kf5_bindir} \
-DINCLUDE_INSTALL_DIR:PATH=%{_includedir} \
-DLIB_INSTALL_DIR:PATH=%{_lib} \
-DKCFG_INSTALL_DIR:PATH=%{_datadir}/config.kcfg \
-DPLUGIN_INSTALL_DIR:PATH=%{_kf5_plugindir} \
-DQT_PLUGIN_INSTALL_DIR:PATH=%{_qt5_plugindir} \
-DQML_INSTALL_DIR:PATH=%{_kf5_qmldir} \
-DIMPORTS_INSTALL_DIR:PATH=%{_qt5_importdir} \
-DECM_MKSPECS_INSTALL_DIR:PATH=%{_kf5_datadir}/qt5/mkspecs/modules \
-DSYSCONF_INSTALL_DIR:PATH=%{_kf5_sysconfdir} \
-DLIBEXEC_INSTALL_DIR:PATH=%{_libexecdir} \
-DKF5_LIBEXEC_INSTALL_DIR=%{_kf5_libexecdir} \
-DKF5_INCLUDE_INSTALL_DIR=%{_kf5_includedir} \
..

# run again if we need a suffix
%if "%{?_lib}" == "lib64"
%{cmake} %{?_cmake_lib_suffix64} ..
%endif

popd

make %{?_smp_mflags} -C %{_target_platform}-Qt5
# << build pre



# >> build post
# << build post

%install
rm -rf %{buildroot}
# >> install pre
make install/fast DESTDIR=%{buildroot} -C %{_target_platform}-Qt5
# << install pre

# >> install post
# << install post

%files
%defattr(-,root,root,-)
%doc COPYING.LIB
%{_qt5_plugindir}/phonon4qt5_backend/*
%{_kf5_iconsdir}/hicolor/*
# >> files
# << files
