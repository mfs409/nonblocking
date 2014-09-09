#!/bin/bash

ratios=(0 34 80 100)
threads=(1 2 3 4 6 8 10 12)
#threads=(1 2 4 8 12 16 24 32 64)
algs=(LFList LFArray SO WFList WFArray)

for ratio in ${ratios[*]}
do
    for alg in ${algs[*]}
    do
        for thr in ${threads[*]}
        do
            M=256
            I=0
            ofile=$alg."R$ratio"."M$M"."I$I".csv
            echo -n $thr >> $ofile
            filename=./raw/output.$alg."R$ratio"."M$M"."I$I"."p$thr".csv
            total=0
            for i in `cat $filename`
            do
                total=$(echo $total + $i | bc)
            done
            avg=$(echo "$total / 5" | bc)
            echo -n , $avg , >> $ofile
            cat $filename | sort -n | head -n 3 | tail -n 1 >> $ofile
            
            M=4096
            I=0
            ofile=$alg."R$ratio"."M$M"."I$I".csv
            echo -n $thr >> $ofile
            filename=./raw/output.$alg."R$ratio"."M$M"."I$I"."p$thr".csv
            total=0
            for i in `cat $filename`
            do
                total=$(echo $total + $i | bc)
            done
            avg=$(echo "$total / 5" | bc)
            echo -n , $avg , >> $ofile
            cat $filename | sort -n | head -n 3 | tail -n 1 >> $ofile

            M=65536
            I=0
            ofile=$alg."R$ratio"."M$M"."I$I".csv
            echo -n $thr >> $ofile
            filename=./raw/output.$alg."R$ratio"."M$M"."I$I"."p$thr".csv
            total=0
            for i in `cat $filename`
            do
                total=$(echo $total + $i | bc)
            done
            avg=$(echo "$total / 5" | bc)
            echo -n , $avg , >> $ofile
            cat $filename | sort -n | head -n 3 | tail -n 1 >> $ofile

            
        done
    done
done
