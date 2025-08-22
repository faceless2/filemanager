#ROOT=/tmp/uploaddir		# optional compiled-in root directory
#LOG=/tmp/logfile		# optional logfile...
#LOG=syslog			# optional log using syslog

TARGET = filemanager
LIBS = -lm -ljansson
CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -static

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -DROOT='"$(ROOT)"' -DLOG='"$(LOG)"' -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@

clean:
	-rm -f *.o
	-rm -f $(TARGET)
