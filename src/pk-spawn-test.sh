
time=0.30

echo -e "percentage\t0" > /dev/stderr
echo -e "percentage\t10" > /dev/stderr
sleep ${time}
echo -e "percentage\t20" > /dev/stderr
sleep ${time}
echo -e "percentage\t30" > /dev/stderr
sleep ${time}
echo -e "percentage\t40" > /dev/stderr
sleep ${time}
echo -e "package:1\tpolkit\tPolicyKit daemon"
echo -e "package:0\tpolkit-gnome\tPolicyKit helper for GNOME"
sleep ${time}
echo -e -n "package:0\tConsoleKit"
sleep ${time}
echo -e "\tSystem console checker"
echo -e "percentage\t50" > /dev/stderr
sleep ${time}
echo -e "percentage\t60" > /dev/stderr
sleep ${time}
echo -e "percentage\t70" > /dev/stderr
sleep ${time}
echo -e "percentage\t80" > /dev/stderr
sleep ${time}
echo -e "percentage\t90" > /dev/stderr
echo -e "package:0\tgnome-power-manager\tMore useless software"
sleep ${time}
echo -e "percentage\t100" > /dev/stderr

