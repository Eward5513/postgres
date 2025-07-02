MODULE_big = tsdmp

# Include only our extension source files (all C++)
OBJS = src/extension.o src/main.o #src/create_index.o src/file_sorter.o

EXTENSION = tsdmp
DATA = tsdmp--1.0.sql
PGFILEDESC = "TSDMP - Time-Spatial Data Management Platform Extension"


# 使用PGXS构建系统（默认）
ifndef NO_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
# 仅在明确禁用PGXS时使用传统构建方式
subdir = contrib/tsdmp
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

# 在包含PGXS后，强制使用C++编译器
override CC = g++
override CXX = g++

# 过滤掉C++编译器不支持的选项
override CFLAGS := $(filter-out -fexcess-precision=standard,$(CFLAGS))
override CPPFLAGS := $(filter-out -fexcess-precision=standard,$(CPPFLAGS))

# 只添加项目特定的头文件路径（不覆盖pg_config的设置）
override CPPFLAGS += -std=c++17 -I./include

# 调试选项
ifdef DEBUG
    override CPPFLAGS += -g -O0 -DDEBUG
else
    override CPPFLAGS += -O2 -DNDEBUG
endif

# 只添加必要的额外链接库（保持pg_config的默认链接设置）
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