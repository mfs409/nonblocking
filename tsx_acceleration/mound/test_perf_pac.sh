for T in dcas skip hunt fgl
do
    for N in 1 2 4 6 8 10 12 14 16
    do
        obj/perftest -T $T -N $N -O 65536 -I 256  >> perfOnTinyBy64K.edat
        obj/perftest -T $T -N $N -O 65536 -I 65536  >> perfOnSmallBy64K.edat
        obj/perftest -T $T -N $N -O 65536 -I 1048576  >> perfOnLargeBy64K.edat
        obj/perftest -T $T -N $N -O 1048576 -I 256  >> perfOnTinyBy1M.edat
        obj/perftest -T $T -N $N -O 1048576 -I 65536  >> perfOnSmallBy1M.edat
        obj/perftest -T $T -N $N -O 1048576 -I 1048576  >> perfOnLargeBy1M.edat
    done
done
