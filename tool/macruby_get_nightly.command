#!/bin/sh
# Download and install the latest version of MacRuby
URL=http://www.macruby.org/files/nightlies/
PKG=macruby_nightly-latest.pkg
DOWNLOAD_DIR=~/Downloads
DESTROOT=/

cd ${DOWNLOAD_DIR}
rm -f ${PKG}
echo "Downloading ${URL}/${PKG} into ${DOWNLOAD_DIR}"
curl -O ${URL}/${PKG}
sudo installer -pkg ${PKG}  -target ${DESTROOT}
