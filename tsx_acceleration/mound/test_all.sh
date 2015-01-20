XTHR=( 1 2 4 6 8 10 12 14 16 )
STHR=( 1 2 4 6 8 10 12 14 16 20 24 28 32 40 48 56 64 72 )

if [ `uname -p` == "sparc" ]
then
    THREADS=${STHR[@]}
else
    THREADS=${XTHR[@]}
fi

for T in dcas skiplin skipqc hunt fgl lazylf
do
    for N in ${THREADS[@]}
    do
        echo `date` $T $N
        obj/perftest -T $T -N $N -O 65536 -I 256  >> use.edat
        obj/inserttest -T $T -N $N -O 65536 -I 0 >> fill.edat
        obj/removetest -T $T -N $N -O 65536 -I `perl -e "print $N*65536"` >> drain.edat
        obj/exalltest -T $T -N $N -I 1048576 >> exall.edat
    done
done
