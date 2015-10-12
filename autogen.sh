#!/bin/sh
# Run this to generate all the initial makefiles, etc.

#srcdir=`dirname $0`
#test -z "$srcdir" && srcdir=.

#PKG_NAME="lightdm-webkit2-greeter"
#REQUIRED_AUTOMAKE_VERSION=1.7

#(test -f $srcdir/configure.ac \
#  && test -d $srcdir/src) || {
#    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
#    echo " top-level lightdm-webkit2-greeter directory"
#    exit 1
#}

#which gnome-autogen.sh || {
#    echo "You need to install gnome-common from the GNOME CVS"
#    exit 1
#}
#USE_GNOME2_MACROS=1 USE_COMMON_DOC_BUILD=yes . gnome-autogen.sh

# Run this to generate all the initial makefiles, etc.
test -n "$srcdir" || srcdir=`dirname "$0"`
test -n "$srcdir" || srcdir=.

olddir=`pwd`

cd $srcdir

(test -f configure.ac) || {
        echo "*** ERROR: Directory "\`$srcdir\'" does not look like the top-level project directory ***"
        exit 1
}

PKG_NAME=`autoconf --trace 'AC_INIT:$1' configure.ac`

if [ "$#" = 0 -a "x$NOCONFIGURE" = "x" ]; then
        echo "*** WARNING: I am going to run \`configure' with no arguments." >&2
        echo "*** If you wish to pass any to it, please specify them on the" >&2
        echo "*** \`$0\' command line." >&2
        echo "" >&2
fi

aclocal --install || exit 1
intltoolize --force --copy --automake || exit 1
autoreconf --verbose --force --install -Wno-portability || exit 1

cd $olddir

if [ "$NOCONFIGURE" = "" ]; then
        $srcdir/configure "$@" || exit 1

        if [ "$1" = "--help" ]; then exit 0 else
                echo "Now type \`make\' to compile $PKG_NAME" || exit 1
        fi
else
        echo "Skipping configure process."
fi
