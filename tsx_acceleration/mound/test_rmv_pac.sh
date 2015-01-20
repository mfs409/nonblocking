for T in dcas skip hunt fgl
do
    for N in 1 2 4 6 8 10 12 14 16
    do
        obj/removetest -T $T -N $N -O 65536 -I `perl -e "print $N*65536"`  >> drainFromSmallBy64K.edat
        obj/removetest -T $T -N $N -O 65536 -I `perl -e "print $N*65536*2"` >> rmvFromSmallBy64K.edat
        obj/removetest -T $T -N $N -O 65536 -I `perl -e "print $N*1048576*2"` >> rmvFromLargeBy64K.edat
        obj/removetest -T $T -N $N -O 1048576 -I `perl -e "print $N*1048576"`  >> drainFromLargeBy1M.edat
        obj/removetest -T $T -N $N -O 1048576 -I `perl -e "print $N*1048576*2"` >> rmvFromLargeBy1M.edat
    done
done
