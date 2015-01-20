#!/bin/bash

#threads=(1 2 4 8 10 16 20 24 32 40 48 64)
threads=(1 2 3 4 6 8 10 12 16)
bench=(Q64W2D7 Q64W4D4 Q64W8D3 L64W2D7 L64W4D4 L64W8D3 List fArray SkipList)

for i in `seq 5`
do
    for x in ${bench[@]}
    do
        for t in ${threads[@]}
        do
            echo generating $x trial $i
            obj/mindicatortest -d5 -t$x -p$t >> mind."$x"."$i".csv
        done
    done
done


