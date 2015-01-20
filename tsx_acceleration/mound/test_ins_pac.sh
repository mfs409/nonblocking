for T in dcas skip hunt fgl
do
    for N in 1 2 4 6 8 10 12 14 16
    do
        obj/inserttest -T $T -N $N -O 65536 -I 0  >> addFromZeroBy64K.edat
        obj/inserttest -T $T -N $N -O 65536 -I 65536 >> addFromSmallBy64K.edat
        obj/inserttest -T $T -N $N -O 65536 -I 1048576 >> addFromLargeBy64K.edat
        obj/inserttest -T $T -N $N -O 1048576 -I 0 >> addFromZeroBy1M.edat
        obj/inserttest -T $T -N $N -O 1048576 -I 65536 >> addFromSmallBy1M.edat
        obj/inserttest -T $T -N $N -O 1048576 -I 1048576 >> addFromLargeBy1M.edat
    done
done
