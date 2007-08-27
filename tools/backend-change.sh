make clean
./autogen.sh --prefix=/home/hughsie/.root --with-backend=$1
make
make install

