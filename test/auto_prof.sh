#/bin/bash

emaoe=1000000
fpr=0.00001

if [ $# -eq 0 ]; then
    target=10.69.3.23:12345
else
    target=$1
fi

for i in 1 4 8 16 32 64 96 128; do 
    ./set_performance_test.out $i 1000000 $emaoe $fpr $target
    ./get_performance_test.out $i 1000000 $emaoe $fpr $target
done

