export G_DEBUG=fatal_criticals
killall packagekitd
./packagekitd --verbose --no-daemon | tee debug.log

