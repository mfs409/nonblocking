#!/bin/bash

ratios=(0 34 80 100)
threads=(1 2 3 4 6 8 10 12)
algs=(LF WF Harris HarrisRTTI Lazy)

for ratio in ${ratios[*]}
do
    for alg in ${algs[*]}
    do
        for thr in ${threads[*]}
        do

            M=4096
            I=2048
            ofile=$alg."R$ratio"."M$M"."I$I".csv
            echo -n $thr, >> $ofile
            for i in `seq 1`
            do
                filename=./raw/output.$alg."R$ratio"."M$M"."I$I"."p$thr".csv
                cat $filename >> $ofile
            done

            M=65536
            I=32768
            ofile=$alg."R$ratio"."M$M"."I$I".csv
            echo -n $thr, >> $ofile
            for i in `seq 1`
            do
                filename=./raw/output.$alg."R$ratio"."M$M"."I$I"."p$thr".csv
                cat $filename >> $ofile
            done
            
        done
    done
done
