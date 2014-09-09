#!/bin/bash

ratios=(0 10 34 50 80 90 100)
threads=(1 2 3 4 6 8 10 12)
#threads=(1 2 4 8 12 16 24 32 48 64)
algs=(LFList LFArray LFArrayOpt SO AdaptiveArray AdaptiveArrayOpt WFList WFArray)

for ratio in ${ratios[*]}
do
    for alg in ${algs[*]}
    do
        for thr in ${threads[*]}
        do
            for i in `seq 5`
            do
                M=256
                I=0
                filename=output.$alg."R$ratio"."M$M"."I$I"."p$thr".csv
                echo $filename
                java -cp ..:../lib/java-getopt-1.0.13.jar SetBench -d 5 -a $alg -R $ratio -p $thr -M $M -I $I | cut -d':' -f2 >> $filename

                M=4096
                I=0
                filename=output.$alg."R$ratio"."M$M"."I$I"."p$thr".csv
                echo $filename
                java -cp ..:../lib/java-getopt-1.0.13.jar SetBench -d 5 -a $alg -R $ratio -p $thr -M $M -I $I | cut -d':' -f2 >> $filename
                
                M=65536
                I=0
                filename=output.$alg."R$ratio"."M$M"."I$I"."p$thr".csv
                echo $filename
                java -cp ..:../lib/java-getopt-1.0.13.jar SetBench -d 5 -a $alg -R $ratio -p $thr -M $M -I $I | cut -d':' -f2 >> $filename
            done
        done
    done
done
