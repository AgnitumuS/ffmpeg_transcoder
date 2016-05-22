FLAGS := -O3 -Wall 

.PHONY : clean

transcoder: transcoder.o list.o patch_memory.o
	gcc $(FLAGS) patch_memory.o list.o  transcoder.o  -o transcoder -L /usr/local/lib \
	-lavdevice -lavformat -lavfilter -lavcodec -lswresample -lswscale \
	-lavutil -lm -lpthread

transcoder.o: transcoder.c 
	gcc $(FLAGS) -I /usr/local/include -c -o transcoder.o \
	transcoder.c 

list.o: list.c
	gcc $(FLAGS) -c -o list.o list.c 

patch_memory.o: patch_memory.c patch_memory.h
	gcc $(FLAGS) -c -o patch_memory.o patch_memory.c

clean:
	rm -f *.o transcoder
