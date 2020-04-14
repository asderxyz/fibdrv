#!/bin/bash

DRV_PATH=/home/adrian/archive/jserv/linux2020/fibdrv

out_path=`dirname $0`

client_log=${out_path}/client.log
kern_log=${out_path}/kern_fib.log
user_log=${out_path}/data_user_kern.log

rmmod fibdrv && insmod $DRV_PATH/fibdrv.ko
./client > $client_log
cat /sys/kernel/fibonacci/fib_time > $kern_log

function gen_data {
    local IFS=$'\n'
    local user_total_time=($1)
    local kernel_fib_time=($2)

    echo -n "" > $user_log

    for (( i = 0; i < ${#user_total_time[@]}; i++)) do
        kern_to_user=$((${user_total_time[$i]}-${kernel_fib_time[$i]}))
        echo "$i, ${user_total_time[$i]} ${kernel_fib_time[$i]} $kern_to_user" >> $user_log
    done
}

user_total=`grep offset $client_log | cut -d ':' -f 2 | cut -d ' ' -f 2`
kernel_fib=`cat /sys/kernel/fibonacci/fib_time | cut -d ':' -f 2 | cut -d ' ' -f 2`

gen_data "$user_total" "$kernel_fib"

cd $out_path
gnuplot data.gp
