export G_DEBUG=fatal_criticals
killall packagekitd
./packagekitd --verbose --disable-timer --backend=dummy | tee debug.log

