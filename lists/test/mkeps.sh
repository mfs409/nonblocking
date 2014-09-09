#!/usr/bin/env gnuplot
set terminal postscript eps monochrome 'Helvetica' 20
set style data linespoints
set ylabel 'Throughput (ops/ms)' font 'Helvetica,20'
set xlabel 'Threads' font 'Helvetica,20'
set pointsize 2
set key top left
set key width -2
set ytic auto
set yrange[0:*]
set xrange[0.8:12.2]
set xtics 4

# glyphs (the 'pt' parameter)
# 1 is plus
# 2 is x
# 3 is star
# 4 is empty square
# 5 is filled square
# 6 is empty circle
# 7 is filled circle
# 8 is empty up triangle
# 9 is filled up triangle
# 10 is empty down triangle
# 11 is full down triangle
# 12 is empty diamond
# 13 is full diamond
# 14 is empty pentagon
# 15 is full pentagon

set output 'R0-M4096-I2048.eps'
plot \
    'Harris.R0.M4096.I2048.csv'             u 1:($2) pt 5 lw 1.5 t 'Harris',   \
    'HarrisRTTI.R0.M4096.I2048.csv'         u 1:($2) pt 4 lw 1.5 t 'HarrisRTTI',   \
    'Lazy.R0.M4096.I2048.csv'               u 1:($2) pt 6 lw 1.5 t 'LazyList',    \
    'LF.R0.M4096.I2048.csv'                 u 1:($2) pt 10 lw 1.5 t 'LFList', \
    'WF.R0.M4096.I2048.csv'                 u 1:($2) pt 8 lw 1.5 t 'WFList'

set output 'R34-M4096-I2048.eps'
plot \
    'Harris.R34.M4096.I2048.csv'             u 1:($2) pt 5 lw 1.5 t 'Harris',   \
    'HarrisRTTI.R34.M4096.I2048.csv'         u 1:($2) pt 4 lw 1.5 t 'HarrisRTTI',   \
    'Lazy.R34.M4096.I2048.csv'               u 1:($2) pt 6 lw 1.5 t 'LazyList',    \
    'LF.R34.M4096.I2048.csv'                 u 1:($2) pt 10 lw 1.5 t 'LFList', \
    'WF.R34.M4096.I2048.csv'                 u 1:($2) pt 8 lw 1.5 t 'WFList'

set output 'R100-M4096-I2048.eps'
plot \
    'Harris.R100.M4096.I2048.csv'             u 1:($2) pt 5 lw 1.5 t 'Harris',   \
    'HarrisRTTI.R100.M4096.I2048.csv'         u 1:($2) pt 4 lw 1.5 t 'HarrisRTTI',   \
    'Lazy.R100.M4096.I2048.csv'               u 1:($2) pt 6 lw 1.5 t 'LazyList',    \
    'LF.R100.M4096.I2048.csv'                 u 1:($2) pt 10 lw 1.5 t 'LFList', \
    'WF.R100.M4096.I2048.csv'                 u 1:($2) pt 8 lw 1.5 t 'WFList'

set output 'R80-M4096-I2048.eps'
plot \
    'Harris.R80.M4096.I2048.csv'             u 1:($2) pt 5 lw 1.5 t 'Harris',   \
    'HarrisRTTI.R80.M4096.I2048.csv'         u 1:($2) pt 4 lw 1.5 t 'HarrisRTTI',   \
    'Lazy.R80.M4096.I2048.csv'               u 1:($2) pt 6 lw 1.5 t 'LazyList',    \
    'LF.R80.M4096.I2048.csv'                 u 1:($2) pt 10 lw 1.5 t 'LFList', \
    'WF.R80.M4096.I2048.csv'                 u 1:($2) pt 8 lw 1.5 t 'WFList'



set output 'R0-M65536-I32768.eps'
plot \
    'Harris.R0.M65536.I32768.csv'             u 1:($2) pt 5 lw 1.5 t 'Harris',   \
    'HarrisRTTI.R0.M65536.I32768.csv'         u 1:($2) pt 4 lw 1.5 t 'HarrisRTTI',   \
    'Lazy.R0.M65536.I32768.csv'               u 1:($2) pt 6 lw 1.5 t 'LazyList',    \
    'LF.R0.M65536.I32768.csv'                 u 1:($2) pt 10 lw 1.5 t 'LFList', \
    'WF.R0.M65536.I32768.csv'                 u 1:($2) pt 8 lw 1.5 t 'WFList'

set output 'R34-M65536-I32768.eps'
plot \
    'Harris.R34.M65536.I32768.csv'             u 1:($2) pt 5 lw 1.5 t 'Harris',   \
    'HarrisRTTI.R34.M65536.I32768.csv'         u 1:($2) pt 4 lw 1.5 t 'HarrisRTTI',   \
    'Lazy.R34.M65536.I32768.csv'               u 1:($2) pt 6 lw 1.5 t 'LazyList',    \
    'LF.R34.M65536.I32768.csv'                 u 1:($2) pt 10 lw 1.5 t 'LFList', \
    'WF.R34.M65536.I32768.csv'                 u 1:($2) pt 8 lw 1.5 t 'WFList'

set output 'R100-M65536-I32768.eps'
plot \
    'Harris.R100.M65536.I32768.csv'             u 1:($2) pt 5 lw 1.5 t 'Harris',   \
    'HarrisRTTI.R100.M65536.I32768.csv'         u 1:($2) pt 4 lw 1.5 t 'HarrisRTTI',   \
    'Lazy.R100.M65536.I32768.csv'               u 1:($2) pt 6 lw 1.5 t 'LazyList',    \
    'LF.R100.M65536.I32768.csv'                 u 1:($2) pt 10 lw 1.5 t 'LFList', \
    'WF.R100.M65536.I32768.csv'                 u 1:($2) pt 8 lw 1.5 t 'WFList'

set output 'R80-M65536-I32768.eps'
plot \
    'Harris.R80.M65536.I32768.csv'             u 1:($2) pt 5 lw 1.5 t 'Harris',   \
    'HarrisRTTI.R80.M65536.I32768.csv'         u 1:($2) pt 4 lw 1.5 t 'HarrisRTTI',   \
    'Lazy.R80.M65536.I32768.csv'               u 1:($2) pt 6 lw 1.5 t 'LazyList',    \
    'LF.R80.M65536.I32768.csv'                 u 1:($2) pt 10 lw 1.5 t 'LFList', \
    'WF.R80.M65536.I32768.csv'                 u 1:($2) pt 8 lw 1.5 t 'WFList'

