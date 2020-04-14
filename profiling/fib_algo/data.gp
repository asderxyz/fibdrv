set title "Intel(R) Xeon(R) CPU E5-2695 v4 @ 2.10GHz -\n 72 cores (HT enabled): 2-socket Broadwell CPU"
set xlabel "Fibonacci number: Fn"
set ylabel "Time Cost (ns)"
set term png enhanced font 'Verdana,10'
set output "fib-algo.png"
set key left

plot \
"orig.log" using 1:2 with linespoints linewidth 2 title "orig", \
"fast_doubling.log" using 1:2 with linespoints linewidth 2 title "fast doubling", \
"fast_doubling_clz.log" using 1:2 with linespoints linewidth 2 title "fast doubling with clz",
