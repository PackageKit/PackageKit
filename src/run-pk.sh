export G_DEBUG=fatal_criticals
killall packagekitd
./packagekitd --verbose | tee debug.log

