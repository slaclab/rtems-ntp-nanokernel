#! /bin/csh
#
# test different hz values
#
kern -s 4000 -p 1000 -t 6 -z 1024       # digital alpha
kern -s 4000 -p 1000 -t 6 -z 100        # sun, etc.
#
# test extreme phase/frequency envelope
#
kern -s 4000 -p 500000 -f 500 -t 0
kern -s 4000 -p 500000 -f -500 -t 0
kern -s 4000 -p -500000 -f 500 -t 0
kern -s 4000 -p -500000 -f -500 -t 0
#
# test for overflow at different poll intervals
#
kern -s 1000 -p 500000 -t 0
kern -s 3200 -p 500000 -t 6
kern -s 50000 -p 500000 -t 10
#
# test fll at different poll intervals
#
kern -s 1000 -p 1000 -f 0 -l 256
kern -s 4000 -p 1000 -f 0 -l 1024
kern -s 10000 -p 1000 -f 0 -l 4096
#
# test fll at extreme phase/frequency envelope
#
kern -s 15000 -p 500000 -f 500 -l 256
kern -s 15000 -p 500000 -f -500 -l 256
kern -s 15000 -p -500000 -f 500 -l 256
kern -s 15000 -p -500000 -f -500 -l 256
#
# test PPS
#
kern -c 7 -s 4000 -p 250000 -f 250 -t 0
kern -c 7 -s 4000 -p -250000 -f -250 -t 0
