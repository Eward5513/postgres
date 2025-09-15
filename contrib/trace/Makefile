MODULE_big = trace

# Extension source files (C++)
OBJS = src/extension.o \
       src/trace.o \
       src/data_loader.o \
       src/index_storage.o \
       src/index_builder.o \
       src/morton_utils.o \
       src/pgutils.o \
       src/utils.o \
       src/safe_logger.o

# Extension metadata
EXTENSION = trace
DATA = trace--1.0.sql
PGFILEDESC = "TRACE - Time-Spatial Trajectory Analysis and Computation Engine Extension"

# Regression tests
REGRESS = trace_basic trace_index trace_range_query trace_knn_query trace_buffer_query

# Build system
subdir = contrib/trace
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk

# C++ compiler settings
override CFLAGS := $(filter-out -fexcess-precision=standard,$(CFLAGS))
override CPPFLAGS := $(filter-out -fexcess-precision=standard,$(CPPFLAGS))
override CPPFLAGS += -std=c++17 -I./include
override SHLIB_LINK += -lstdc++

# Debug/Release settings
ifdef DEBUG
    override CPPFLAGS += -g -O0 -DDEBUG
else
    override CPPFLAGS += -O2 -DNDEBUG
endif

# C++ compilation rule
src/%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# Clean rules
clean-extra:
	rm -f src/*.o include/*~ sql/*~ expected/*~

clean: clean-extra

.PHONY: clean-extra