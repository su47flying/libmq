#!/bin/sh

for((i=1;i<=5;i++))
do
   ./obj/esmq.cli 1000000&
done
