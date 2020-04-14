set title "Intel(R) Xeon(R) CPU E5-2695 v4 \n @ 2.10GHz - 72 cores (HT enabled): 2-socket Broadwell CPU"
set xlabel "Fibonacci number: Fn"
set ylabel "Time Cost (ns)"
set term png enhanced font 'Verdana,10'
set output "fib_user_kernel.png"
set key right

plot \
"data_user_kern.log" using 1:2 with linespoints linewidth 2 title "exec time in user space", \
"data_user_kern.log" using 1:3 with linespoints linewidth 2 title "exec time in kernel space", \
"data_user_kern.log" using 1:4 with linespoints linewidth 2 title "transfer time between user and kernel space", \
