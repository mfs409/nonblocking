obj/perftest -T seq -I 65536 -O 1048576 > perfMedWaste.etxt
obj/perftest -T seq -I 1048576 -O 1048576 > perfLgWaste.etxt
obj/perftest -T seq -I 256 -O 1048576 > perfSmallWaste.etxt
obj/sizetest -O 1048576 -T shrink > insShrinkWaste.etxt
obj/sizetest -O 1048576 -T grow > insGrowWaste.etxt
obj/sizetest -O 1048576 -T rand > insRandWaste.etxt
