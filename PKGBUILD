# Maintainer: Dale Whinham <daleyo@gmail.com>

pkgname=print-wifi-nl-git
pkgver=0.0.1.r0.g107203c
pkgrel=1
pkgdesc="A utility to display Wi-Fi connection information via libnl"
arch=('i686' 'x86_64')
url="https://github.com/dwhinham/print-wifi-nl"
license=('GPL')
groups=('base')
depends=('libnl')
makedepends=('cmake')
source=('print-wifi-nl-git::git://github.com/dwhinham/print-wifi-nl')
sha1sums=('SKIP')

pkgver() {
  cd $pkgname
  git describe --long | sed 's/\([^-]*-g\)/r\1/;s/-/./g'
}

build() {
        cd "$srcdir/$pkgname"
        mkdir build
        cd build
        cmake ..
        make
}

package() {
        cd "$srcdir/$pkgname/build"
        make DESTDIR="$pkgdir/" install
}
