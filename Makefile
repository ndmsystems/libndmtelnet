.PHONY: all clean

LIBRARY    := libndmtelnet.a

SRC_DIR    := . \
              contrib/ylib \
              contrib/libtelnet \
              src

OBJS       := $(foreach d,$(SRC_DIR),$(patsubst %.c,%.o,$(wildcard $d/*.c)))
LIB_OBJS   := $(filter-out $(EXE_OBJS),$(OBJS))
INC_LIST   := -I./contrib -I./include

CPPFLAGS   ?= -D_LARGEFILE_SOURCE \
              -D_LARGEFILE64_SOURCE \
              -D_FILE_OFFSET_BITS=64 \
              -D_POSIX_C_SOURCE=200112L \
              -D_BSD_SOURCE \
              -D_XOPEN_SOURCE=600 \
              -D_DEFAULT_SOURCE \
              $(INC_LIST) \
              -MMD

COPTS      := -O2 -flto

CFLAGS     ?= -pipe -fPIC -std=c99 \
              -Wall \
              -Wconversion \
              -Winit-self \
              -Wmissing-field-initializers \
              -Wpointer-arith \
              -Wredundant-decls \
              -Wshadow \
              -Wstack-protector \
              -Wswitch-enum \
              -Wundef \
              -fdata-sections \
              -ffunction-sections \
              -fstack-protector-all \
              -ftabstop=4 \
              $(COPTS)

#CFLAGS    += -fsanitize=address \
              -fsanitize=undefined

LDFLAGS    ?= $(COPTS) -lc

#LDFLAGS   += -fsanitize=address \
              -fsanitize=undefined

all: $(LIBRARY)

$(OBJS): %.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIBRARY): $(LIB_OBJS)
	$(AR) sr $@ $^

clean:
	rm -fv *~ $(LIBRARY) $(OBJS) $(OBJS:.o=.d)

-include $(OBJS:.o=.d)
