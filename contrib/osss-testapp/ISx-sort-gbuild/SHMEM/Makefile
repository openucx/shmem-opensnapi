# Copyright (c) 2018 - 2020 Arm Ltd

CC = cc
LD = $(CC)
DEBUGFLAGS = -g -p -O0 -DDEBUG
#OPTFLAGS = -O3 -DNDEBUG -mavx
OPTFLAGS = -O2 -DNDEBUG
override CFLAGS += -Wall -Wextra -std=c99 #$(OPTFLAGS)
override LDLIBS += -lrt -lm
override LDFLAGS += 
### 
# If you change these values, you must also change them in params.h
STRONG = 1
WEAK = 2
WEAKISO = 3
###

.PHONY: all clean debug

COMMON_SRC := pcg_basic.c timer.c
HDR := $(wildcard *.h)

OBJDIR := obj
BINDIR := bin

STRONG_EXE = isx-strong.x gbuild-strong.x

WEAK_EXE = isx-weak.x gbuild-weak.x

WEAKISO_EXE = isx-weakiso.x gbuild-weakiso.x

#ALL_EXE = $(STRONG_EXE) $(WEAK_EXE) $(WEAKISO_EXE)
ALL_EXE = $(WEAK_EXE)

COMMON_OBJ := $(COMMON_SRC:.c=.o)
COMMON_OBJ := $(addprefix $(OBJDIR)/,$(COMMON_OBJ))

all: $(OBJDIR) $(BINDIR) $(ALL_EXE)

optimized: CFLAGS += $(OPTFLAGS)
optimized: all

debug: CFLAGS += $(DEBUGFLAGS)
debug: all

shmemio: CFLAGS += -DUSING_SHMEMIO $(OPTFLAGS)
shmemio: all

shmemio-debug: CFLAGS += -DUSING_SHMEMIO $(DEBUGFLAGS)
shmemio-debug: all

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

$(OBJDIR)/%.o : %.c 
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%-w.o : %.c 
	$(CC) $(CFLAGS) -D SCALING_OPTION=$(WEAK) -c $< -o $@

$(OBJDIR)/%-s.o : %.c 
	$(CC) $(CFLAGS) -D SCALING_OPTION=$(STRONG) -c $< -o $@

$(OBJDIR)/%-wi.o : %.c 
	$(CC) $(CFLAGS) -D SCALING_OPTION=$(WEAKISO) -c $< -o $@

%-weak.x: $(COMMON_OBJ) $(OBJDIR)/%-w.o 
	$(LD) $^ -o $(BINDIR)/$@ $(LDLIBS) $(LDFLAGS)

%-strong.x: $(COMMON_OBJ) $(OBJDIR)/%-s.o 
	$(LD) $^ -o $(BINDIR)/$@ $(LDLIBS) $(LDFLAGS)

%-weakiso.x: $(COMMON_OBJ) $(OBJDIR)/%-wi.o 
	$(LD) $^ -o $(BINDIR)/$@ $(LDLIBS) $(LDFLAGS)

tidy: .FORCE
	rm -f *~

clean: tidy
	rm -f $(OBJDIR)/*.o

scrub: clean
	rm -rf $(BINDIR) $(OBJDIR)

depend:
	makedepend -- $(CFLAGS) -- $(SRCS)

print: .FORCE
	echo "COMMON_SRC " $(COMMON_SRC)
	echo "COMMON_OBJ " $(COMMON_OBJ)

.FORCE:

# Don't place anything below this line, since
# # the make depend program will overwrite it
# # DO NOT DELETE THIS LINE -- make depend depends on it.
