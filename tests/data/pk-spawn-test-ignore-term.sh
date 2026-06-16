#!/usr/bin/env bash

# Deliberately ignore SIGTERM so the daemon has to escalate to SIGKILL.
trap '' TERM

time=0.3

i=0
while true; do
	echo -e "percentage\t$((i % 100))"
	i=$((i + 10))
	sleep ${time}
done
