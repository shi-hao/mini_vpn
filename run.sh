#!/bin/bash

./minivpn 39.105.113.152 1194  tun client &
ifconfig tun0 10.8.0.2  netmask 255.0.0.0
#ifconfig
route add -net 10.8.0.0 netmask 255.255.0.0  tun0
#route -n
