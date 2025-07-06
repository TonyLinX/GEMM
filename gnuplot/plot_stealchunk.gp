set terminal png size 640,480
set output 'gnuplot/stealchunk_plot.png'

set title 'STEAL CHUNK Impact on Throughput'
set xlabel 'STEAL CHUNK size'
set ylabel 'Throughput (Ops/s)'
set grid
set style data linespoints
set pointsize 1.5

plot 'throughput_stealchunk.txt' using 1:(1/$2) title 'lockfree_{rr}' lt rgb 'blue' pt 7