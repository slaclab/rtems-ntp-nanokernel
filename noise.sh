#! /bin/csh
#
# Make synthetic noise generator and test.
#
gcc noise.c gauss.c /lib/libm.a -o noise
noise -f 1e-8 -s 20
