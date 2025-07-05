MODULE_big = tsdmp

# Include all our extension source files (all C++)
OBJS = src/extension.o \
       src/main.o \
       src/data_loader.o \
       src/file_sorter.o \
       src/octree_node.o \
       src/userdata_manager.o \
       src/kdtree_node_manager.o \
       src/octree_node_manager.o \
       src/mesh_connection_manager.o \
       src/pgutils.o \
       src/spatiotemporal_data.o \
       src/utils.o \
       src/safe_logger.o \
       src/original_data_manager.o

EXTENSION = tsdmp
DATA = tsdmp--1.0.sql
PGFILEDESC = "TSDMP - Time-Spatial Data Management Platform Extension"


# 使用PGXS构建系统（默认）
# ifndef NO_PGXS
# PG_CONFIG = pg_config
# PGXS := $(shell $(PG_CONFIG) --pgxs)
# include $(PGXS)
# else
# 仅在明确禁用PGXS时使用传统构建方式
subdir = contrib/tsdmp
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
# endif

# 不要完全覆盖CC，而是在需要时使用CXX
# 让PGXS处理编译器选择，只添加C++特定的设置

# 过滤掉C++编译器不支持的选项
override CFLAGS := $(filter-out -fexcess-precision=standard,$(CFLAGS))
override CPPFLAGS := $(filter-out -fexcess-precision=standard,$(CPPFLAGS))

# 添加C++17支持和项目头文件路径
override CPPFLAGS += -std=c++17 -I./include

# 调试选项
ifdef DEBUG
    override CPPFLAGS += -g -O0 -DDEBUG
else
    override CPPFLAGS += -O2 -DNDEBUG
endif

# 添加C++链接库
override SHLIB_LINK += -lstdc++

# 编译扩展源文件的规则（C++）
src/%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# 自定义清理规则
clean-extra:
	rm -f src/*.o include/*~ test/*~

clean: clean-extra

# 安装后测试
installcheck: install
	$(MAKE) -C test installcheck

.PHONY: clean-extra installcheck 