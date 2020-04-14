#!/bin/bash

DRV_PATH=/home/adrian/archive/jserv/linux2020/fibdrv

out_path=`dirname $0`

algo=(orig fast_doubling fast_doubling_clz fast_doubling_clz_wo_multiply)

function gen_data {
    local IFS=$'\n'
    local kernel_fib_time=($1)
    local file_name=($2)
    local i

    echo -n "" > ${out_path}/${file_name}.log

    for (( i = 0; i < ${#kernel_fib_time[@]}; i++)) do
        echo "$i, ${kernel_fib_time[$i]}" >> ${out_path}/${file_name}.log
    done
}


rmmod fibdrv && insmod $DRV_PATH/fibdrv.ko

for (( i = 0; i < ${#algo[@]}; i++)) do
    echo $i > /sys/kernel/fibonacci/fib_algo
    ./client
    kernel_fib=`cat /sys/kernel/fibonacci/fib_time | cut -d ':' -f 2 | cut -d ' ' -f 2`
    gen_data "$kernel_fib" "${algo[$i]}"
done

cd $out_path
gnuplot data.gp
