#!/bin/bash

iface=$1
rate=$2

if [ "`whoami`" != "root" ]; then
	echo "Must be root"
	exit 1
fi

if [ -z "$iface" ]; then
	echo "Usage: $0 <iface> [rate]"
	exit 1
fi

ifaces=`ifconfig | grep -oE "(eth[0-9]+|wlan0)"`

for i in $ifaces __invalid__; do
	if [ "$i" == "$iface" ]; then
		break
	fi
done

if [ "$i" == "__invalid__" ]; then
	echo "Interface $iface not found"
	echo "Available: $ifaces"
	exit 1
fi

if [ -z "$rate" ]; then
	echo "Using default bandwidth"
	rate=200kbit
fi

sysctl -w kernel.panic=20
tc qdisc del dev $iface root
tc qdisc add dev $iface root handle 1: htb default 10
tc class add dev $iface parent 1: classid 1:10 htb rate $rate ceil $rate burst 1520
tc qdisc add dev $iface parent 1:10 pfifo limit 10
