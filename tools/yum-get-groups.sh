cat ../backends/yum/helpers/yumBackend.py | grep "GROUP_" | cut -d":" -f2 | cut -d"," -f1 | cut -d" " -f2 | sort| uniq

