# 编译器设置
CXX = g++
# 获取wxWidgets的编译和链接标志
CXXFLAGS = $(shell wx-config --cxxflags)
# 添加 setupapi 库到链接选项中
LIBS = $(shell wx-config --libs) -lsetupapi

# 目标文件
TARGET = merger 

# 源文件
SRCS = merge_main.cpp
RC = app.rc

# Windows资源编译器
WINDRES = windres
# 添加 wxWidgets 包含路径
WINDRES_FLAGS = --include-dir=D:/msys64/ucrt64/include/wx-3.2

# 目标规则
$(TARGET): $(SRCS) $(RC)
	$(WINDRES) $(WINDRES_FLAGS) $(RC) -O coff -o resources.res
	$(CXX) $(SRCS) resources.res $(CXXFLAGS) $(LIBS) -o $(TARGET)

# 清理规则
clean:
	rm -f $(TARGET) resources.res