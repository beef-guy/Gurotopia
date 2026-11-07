flags := -std=c++20 -g -MMD -MP
includes := -Iinclude -Ibuild/include
libraries := -L./include/enet/lib -L./include/mysql/lib -lssl -lcrypto -lmariadb

binary := main.out
ifeq ($(OS),Windows_NT)
libraries += -lenet_32 -lws2_32 -lwinmm -lcrypt32
binary := main.exe
else # linux
libraries += -lenet
endif

all: $(binary)

sources := main.cpp $(shell find include -type f -name '*.cpp' 2>/dev/null)
objects := $(sources:%.cpp=build/%.o)

# @note Lua 5.4 for server scripts (include/scripting). built as C++ so a script error unwinds C++ code properly
lua_sources := $(wildcard include/lua/*.c)
lua_objects := $(lua_sources:%.c=build/%.o)

$(binary): $(objects) $(lua_objects)
	g++ $(objects) $(lua_objects) -o $@ $(libraries)

build/include/lua/%.o: include/lua/%.c
	@mkdir -p $(dir $@)
	g++ -x c++ -std=c++20 -O2 -MMD -MP -c $< -o $@

pch_hpp := include/pch.hpp
pch_gch := build/include/pch.hpp.gch

$(pch_gch): $(pch_hpp)
	@mkdir -p $(dir $@)
	g++ $(flags) $(includes) -x c++-header $< -o $@

build/%.o: %.cpp $(pch_gch)
	@mkdir -p $(dir $@)
	g++ $(flags) $(includes) -include $(pch_hpp) -c $< -o $@

-include $(objects:.o=.d) $(lua_objects:.o=.d)

# @note tests for the Lua sandbox, against a local MariaDB: make lua-test && ./lua_test.out mysql_login_test.txt
lua-test: $(filter-out build/main.o,$(objects)) $(lua_objects) build/tests/lua_sandbox_test.o
	g++ $^ -o lua_test.out $(libraries)

.PHONY : all clean lua-test
clean :
	-rm -rf build $(binary) lua_test.out
