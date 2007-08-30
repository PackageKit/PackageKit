make clean
./autogen.sh --prefix=/home/hughsie/.root --with-backend=$1 --enable-tests
make
make install

