#!/bin/bash
A="adb -s 5d1ee37e"; D=/dev/input/event9; cd /tmp/claude-1000
$A shell "sendevent $D 3 16 1; sendevent $D 0 0 0; sleep 0.15; sendevent $D 1 304 1; sendevent $D 0 0 0; sleep 0.3; sendevent $D 1 304 0; sendevent $D 0 0 0; sleep 1; sendevent $D 3 16 0; sendevent $D 0 0 0"; sleep 3
