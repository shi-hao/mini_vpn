#!/bin/bash

./minivpn config.cnf &
ifconfig tun0 10.8.10.2  netmask 255.0.0.0
#ifconfig
route add -net 10.8.10.0 netmask 255.255.255.0  tun0
#route -n
