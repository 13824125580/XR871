# Rules for building library

################################################################################
ROOT_PATH = ../..

# Include the common make definitions.
include $(ROOT_PATH)/gcc.mk

################################################################################
LIBS = libxrsys.a

DIRS = .

ifeq ($(__CONFIG_ARCH_DUAL_CORE), 1)
DIRS += ./mbuf ./ducc
endif

SRCS = $(basename $(foreach dir,$(DIRS),$(wildcard $(dir)/*.[csS])))
OBJS = $(addsuffix .o,$(SRCS))

CC_FLAGS += -Werror
# CC_FLAGS += -Wno-format	# suppress printf/scanf format warnnings

################################################################################
.PHONY: all install size clean

all: $(LIBS)

$(LIBS): $(OBJS)
	$(AR) -crs $@ $^

install: $(LIBS)
	$(CP) -t $(ROOT_PATH)/lib $^

size:
	$(SIZE) -t $(LIBS)

clean:
	-rm -f $(LIBS) $(OBJS) $(DEPS)


################################################################################
DEPS = $(OBJS:.o=.d)
-include $(DEPS)
