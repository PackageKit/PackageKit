make clean
./autogen.sh --prefix=/home/hughsie/.root --with-default-backend=$1 --enable-tests
make
make install

