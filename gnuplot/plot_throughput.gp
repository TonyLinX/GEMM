set terminal png size 640,480
set output 'gnuplot/throughput_plot.png'

set title "Lock-free vs. Lock-based Throughput"
set xlabel "Number of threads"
set ylabel "Throughput (Ops/s)"
set grid

set key left top
set style data linespoints
set pointsize 1.5

# 用 1 / time 作為 throughput（反比時間）
plot 'throughput.txt' using 1:(1/$2) title "Lock-based" lt rgb "red" pt 7, \
     'throughput.txt' using 1:(1/$3) title "Lock-free" lt rgb "blue" pt 5
