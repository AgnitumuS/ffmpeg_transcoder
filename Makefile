FLAGS := -O3 -Wall

.PHONY : clean

transcoder: transcoder.o list.o patch_memory.o patch_queue.o
	gcc $(FLAGS) patch_queue.o patch_memory.o list.o  transcoder.o  -o transcoder -L /usr/local/lib \
	-lavdevice -lavformat -lavfilter -lavcodec -lswresample -lswscale \
	-lavutil -lm -lpthread

transcoder.o: transcoder.c 
	gcc $(FLAGS) -I /usr/local/include -c -o transcoder.o \
	transcoder.c 

list.o: list.c
	gcc $(FLAGS) -c -o list.o list.c 

patch_memory.o: patch_memory.c patch_memory.h
	gcc $(FLAGS) -c -o patch_memory.o patch_memory.c

patch_queue.o: patch_queue.c patch_queue.h
	gcc $(FLAGS) -c -o patch_queue.o patch_queue.c

test_memory: test_memory.c patch_memory.o
	gcc $(FLAGS) -o test_memory patch_memory.o test_memory.c

test_queue: test_queue.c patch_queue.o
	gcc $(FLAGS) -o test_queue test_queue.c patch_queue.o \
		-lpthread

clean:
	rm -rf *.o transcoder && rm -r [0-9]0[0-9].mp4_146*
