make
g++ -I. -O3 -c rabincmd.C 
g++ rabincmd.o librabinpoly.a 
mv a.out rabin
