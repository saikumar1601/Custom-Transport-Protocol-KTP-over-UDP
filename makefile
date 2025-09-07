# Name: B.Preetham
# Roll:22Cs10015
# GDrive: https://drive.google.com/drive/folders/1ATi6IspZRnERxlp6zUwT4PrFTid5pJvb?usp=drive_link
# --------------------------------------------------------------------------------------------------
CC = gcc
AR = ar
CFLAGS = -Wall -g
LIBNAME = libksocket.a
OBJS = ksocket.o initksocket.o

%.o: %.c
	$(CC) -c $< -o $@

$(LIBNAME): $(OBJS)
	$(AR) rcs $(LIBNAME) $(OBJS)

user1: user1.c $(LIBNAME) initial
	$(CC) user1.c -L. -lksocket -o user1

initial: initial.c $(LIBNAME)
	$(CC) initial.c -L. -lksocket -o initial

user2: user2.c $(LIBNAME) initial
	$(CC) user2.c -L. -lksocket -o user2

clean:
	rm -f *.o $(LIBNAME) user1 user2 initial output.txt
