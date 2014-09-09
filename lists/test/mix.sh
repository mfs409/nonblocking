#!/bin/bash

ratios=(0 34 80 100)
threads=(1 2 3 4 6 8 10 12)
algs=(LF LF2 WF Adaptive Harris HarrisRTTI Lazy)

for ratio in ${ratios[*]}
do
    for alg in ${algs[*]}
    do
        for thr in ${threads[*]}
        do
            for i in `seq 5`
            do
                M=512
                I=256
                filename=output.$alg."R$ratio"."M$M"."I$I"."p$thr".csv
                echo $filename
                java -cp ..:../lib/java-getopt-1.0.13.jar ListBench -d 5 -a $alg -R $ratio -p $thr -M $M -I $I | cut -d':' -f2 >> $filename

                M=2048
                I=1024
                filename=output.$alg."R$ratio"."M$M"."I$I"."p$thr".csv
                echo $filename
                java -cp ..:../lib/java-getopt-1.0.13.jar ListBench -d 5 -a $alg -R $ratio -p $thr -M $M -I $I | cut -d':' -f2 >> $filename
                
                M=16384
                I=8192
                filename=output.$alg."R$ratio"."M$M"."I$I"."p$thr".csv
                echo $filename
                java -cp ..:../lib/java-getopt-1.0.13.jar ListBench -d 5 -a $alg -R $ratio -p $thr -M $M -I $I | cut -d':' -f2 >> $filename
            done
        done
    done
done
