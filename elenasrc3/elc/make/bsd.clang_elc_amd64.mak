#------------------------------------------------------------------------------#
# ELENA make file                                                              #
#------------------------------------------------------------------------------#

WORKDIR = `pwd`

CC = clang
CXX = clang++
AR = ar
LD = clang++
WINDRES = windres

INC = -I.. -I../../engine -I../../common
CFLAGS = -std=c++20 -m64 -static-libgcc -static-libstdc++
LDFLAGS = -m64 -ldl -fuse-ld=lld

RESINC = 
LIBDIR = 
LIB = 

INC_RELEASE = $(INC)
CFLAGS_RELEASE = $(CFLAGS) -O3
RESINC_RELEASE = $(RESINC)
RCFLAGS_RELEASE = $(RCFLAGS)
LIBDIR_RELEASE = $(LIBDIR)
LIB_RELEASE = $(LIB)
LDFLAGS_RELEASE = $(LDFLAGS) -s
OBJDIR_RELEASE = ../../temp/elena64-cli
DEP_RELEASE = $(OBJDIR_RELEASE)/__/__/engine/target.o $(OBJDIR_RELEASE)/__/__/engine/runtime.o $(OBJDIR_RELEASE)/__/__/engine/runtimecore.o $(OBJDIR_RELEASE)/__/__/engine/dispatch.o $(OBJDIR_RELEASE)/__/__/engine/method.o $(OBJDIR_RELEASE)/__/__/engine/ecode.o $(OBJDIR_RELEASE)/__/__/engine/eir.o $(OBJDIR_RELEASE)/__/__/engine/x86abi.o $(OBJDIR_RELEASE)/__/__/engine/x86machine.o $(OBJDIR_RELEASE)/__/__/engine/x86lowering.o $(OBJDIR_RELEASE)/__/__/engine/x86encoder.o $(OBJDIR_RELEASE)/__/__/engine/x86runtimecore.o $(OBJDIR_RELEASE)/__/__/engine/x86runtimecore_x86.o $(OBJDIR_RELEASE)/__/__/engine/x86runtimecore_amd64.o
LIB_RELEASE += $(OBJDIR_RELEASE)/__/__/engine/target.o $(OBJDIR_RELEASE)/__/__/engine/runtime.o $(OBJDIR_RELEASE)/__/__/engine/runtimecore.o $(OBJDIR_RELEASE)/__/__/engine/dispatch.o $(OBJDIR_RELEASE)/__/__/engine/method.o $(OBJDIR_RELEASE)/__/__/engine/ecode.o $(OBJDIR_RELEASE)/__/__/engine/eir.o $(OBJDIR_RELEASE)/__/__/engine/x86abi.o $(OBJDIR_RELEASE)/__/__/engine/x86machine.o $(OBJDIR_RELEASE)/__/__/engine/x86lowering.o $(OBJDIR_RELEASE)/__/__/engine/x86encoder.o $(OBJDIR_RELEASE)/__/__/engine/x86runtimecore.o $(OBJDIR_RELEASE)/__/__/engine/x86runtimecore_x86.o $(OBJDIR_RELEASE)/__/__/engine/x86runtimecore_amd64.o
OUT_RELEASE = ../../../bin/elena64-cli

OBJ_RELEASE = $(OBJDIR_RELEASE)/__/__/common/config.o $(OBJDIR_RELEASE)/__/__/common/dump.o $(OBJDIR_RELEASE)/__/__/common/files.o $(OBJDIR_RELEASE)/__/__/common/paths.o $(OBJDIR_RELEASE)/__/__/common/ustring.o  $(OBJDIR_RELEASE)/__/__/common/xmltree.o $(OBJDIR_RELEASE)/__/__/engine/bcwriter.o $(OBJDIR_RELEASE)/__/__/engine/codescope.o $(OBJDIR_RELEASE)/__/__/engine/jitcompiler.o $(OBJDIR_RELEASE)/__/__/engine/jitlinker.o $(OBJDIR_RELEASE)/__/__/engine/libman.o $(OBJDIR_RELEASE)/__/__/engine/module.o $(OBJDIR_RELEASE)/__/__/engine/parsertable.o $(OBJDIR_RELEASE)/__/__/engine/section.o $(OBJDIR_RELEASE)/__/__/engine/x86_64compiler.o $(OBJDIR_RELEASE)/__/__/engine/x86compiler.o $(OBJDIR_RELEASE)/__/__/engine/x86helper.o $(OBJDIR_RELEASE)/__/__/engine/syntaxtree.o $(OBJDIR_RELEASE)/__/__/engine/bytecode.o $(OBJDIR_RELEASE)/__/__/engine/xmlprojectbase.o $(OBJDIR_RELEASE)/__/__/engine/linux/presenter.o $(OBJDIR_RELEASE)/__/codeimage.o $(OBJDIR_RELEASE)/__/compiler.o $(OBJDIR_RELEASE)/__/compiling.o $(OBJDIR_RELEASE)/__/derivation.o $(OBJDIR_RELEASE)/__/linux/elc.o $(OBJDIR_RELEASE)/__/linux/elfimage.o $(OBJDIR_RELEASE)/__/linux/elflinker.o $(OBJDIR_RELEASE)/__/linux/elflinker32.o $(OBJDIR_RELEASE)/__/linux/elflinker64.o $(OBJDIR_RELEASE)/__/parser.o $(OBJDIR_RELEASE)/__/separser.o $(OBJDIR_RELEASE)/__/project.o $(OBJDIR_RELEASE)/__/source.o $(OBJDIR_RELEASE)/__/modulescope.o $(OBJDIR_RELEASE)/__/compilerlogic.o $(OBJDIR_RELEASE)/__/linux/elfsyslibloader.o $(OBJDIR_RELEASE)/__/linux/pathmanager.o $(OBJDIR_RELEASE)/__/__/engine/serializer.o $(OBJDIR_RELEASE)/__/__/engine/scriptreader.o $(OBJDIR_RELEASE)/__/cli.o

all: release

clean: clean_release

before_release: 
	test -d ../../../bin || mkdir -p ../../../bin
	test -d $(OBJDIR_RELEASE)/__ || mkdir -p $(OBJDIR_RELEASE)/__
	test -d $(OBJDIR_RELEASE)/__/__/engine || mkdir -p $(OBJDIR_RELEASE)/__/__/engine
	test -d $(OBJDIR_RELEASE)/__/__/engine/linux || mkdir -p $(OBJDIR_RELEASE)/__/__/engine/linux
	test -d $(OBJDIR_RELEASE)/__/linux || mkdir -p $(OBJDIR_RELEASE)/__/linux
	test -d $(OBJDIR_RELEASE)/__/__/common || mkdir -p $(OBJDIR_RELEASE)/__/__/common

after_release: 

release: before_release out_release after_release

out_release: before_release $(OBJ_RELEASE) $(DEP_RELEASE)
	$(LD) $(LIBDIR_RELEASE) -o $(OUT_RELEASE) $(OBJ_RELEASE)  $(LDFLAGS_RELEASE) $(LIB_RELEASE)

$(OBJDIR_RELEASE)/__/__/common/config.o: ../../common/config.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../common/config.cpp -o $(OBJDIR_RELEASE)/__/__/common/config.o

$(OBJDIR_RELEASE)/__/__/common/dump.o: ../../common/dump.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../common/dump.cpp -o $(OBJDIR_RELEASE)/__/__/common/dump.o

$(OBJDIR_RELEASE)/__/__/common/files.o: ../../common/files.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../common/files.cpp -o $(OBJDIR_RELEASE)/__/__/common/files.o 

$(OBJDIR_RELEASE)/__/__/common/paths.o: ../../common/paths.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../common/paths.cpp -o $(OBJDIR_RELEASE)/__/__/common/paths.o 

$(OBJDIR_RELEASE)/__/__/common/ustring.o: ../../common/ustring.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../common/ustring.cpp -o $(OBJDIR_RELEASE)/__/__/common/ustring.o 

$(OBJDIR_RELEASE)/__/__/common/xmltree.o: ../../common/xmltree.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../common/xmltree.cpp -o $(OBJDIR_RELEASE)/__/__/common/xmltree.o  

$(OBJDIR_RELEASE)/__/__/engine/bcwriter.o: ../../engine/bcwriter.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/bcwriter.cpp -o $(OBJDIR_RELEASE)/__/__/engine/bcwriter.o  

$(OBJDIR_RELEASE)/__/__/engine/codescope.o: ../../engine/codescope.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/codescope.cpp -o $(OBJDIR_RELEASE)/__/__/engine/codescope.o   

$(OBJDIR_RELEASE)/__/__/engine/jitcompiler.o: ../../engine/jitcompiler.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/jitcompiler.cpp -o $(OBJDIR_RELEASE)/__/__/engine/jitcompiler.o   

$(OBJDIR_RELEASE)/__/__/engine/jitlinker.o: ../../engine/jitlinker.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/jitlinker.cpp -o $(OBJDIR_RELEASE)/__/__/engine/jitlinker.o   

$(OBJDIR_RELEASE)/__/__/engine/libman.o: ../../engine/libman.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/libman.cpp -o $(OBJDIR_RELEASE)/__/__/engine/libman.o

$(OBJDIR_RELEASE)/__/__/engine/module.o: ../../engine/module.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/module.cpp -o $(OBJDIR_RELEASE)/__/__/engine/module.o

$(OBJDIR_RELEASE)/__/__/engine/parsertable.o: ../../engine/parsertable.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/parsertable.cpp -o $(OBJDIR_RELEASE)/__/__/engine/parsertable.o

$(OBJDIR_RELEASE)/__/__/engine/section.o: ../../engine/section.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/section.cpp -o $(OBJDIR_RELEASE)/__/__/engine/section.o

$(OBJDIR_RELEASE)/__/__/engine/x86_64compiler.o: ../../engine/x86_64compiler.cpp ../../engine/x86_64compiler.h ../../codegen/runtime.h ../../codegen/x86/abi.h ../../codegen/x86/runtimecore.h ../../engine/x86runtimecore.h ../../codegen/x86/lowering.h
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/x86_64compiler.cpp -o $(OBJDIR_RELEASE)/__/__/engine/x86_64compiler.o

$(OBJDIR_RELEASE)/__/__/engine/x86compiler.o: ../../engine/x86compiler.cpp ../../engine/x86compiler.h ../../codegen/runtime.h ../../codegen/x86/abi.h ../../engine/x86runtimecore.h ../../codegen/x86/lowering.h
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/x86compiler.cpp -o $(OBJDIR_RELEASE)/__/__/engine/x86compiler.o

$(OBJDIR_RELEASE)/__/__/engine/x86helper.o: ../../engine/x86helper.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/x86helper.cpp -o $(OBJDIR_RELEASE)/__/__/engine/x86helper.o

$(OBJDIR_RELEASE)/__/__/engine/syntaxtree.o: ../../engine/syntaxtree.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/syntaxtree.cpp -o $(OBJDIR_RELEASE)/__/__/engine/syntaxtree.o

$(OBJDIR_RELEASE)/__/__/engine/target.o: ../../codegen/target.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/target.cpp -o $(OBJDIR_RELEASE)/__/__/engine/target.o

$(OBJDIR_RELEASE)/__/__/engine/runtime.o: ../../codegen/runtime.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/runtime.cpp -o $(OBJDIR_RELEASE)/__/__/engine/runtime.o

$(OBJDIR_RELEASE)/__/__/engine/runtimecore.o: ../../codegen/runtimecore.cpp ../../codegen/runtimecore.h
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/runtimecore.cpp -o $(OBJDIR_RELEASE)/__/__/engine/runtimecore.o

$(OBJDIR_RELEASE)/__/__/engine/dispatch.o: ../../codegen/dispatch.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/dispatch.cpp -o $(OBJDIR_RELEASE)/__/__/engine/dispatch.o

$(OBJDIR_RELEASE)/__/__/engine/method.o: ../../codegen/method.cpp ../../codegen/method.h
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/method.cpp -o $(OBJDIR_RELEASE)/__/__/engine/method.o

$(OBJDIR_RELEASE)/__/__/engine/ecode.o: ../../codegen/ecode.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/ecode.cpp -o $(OBJDIR_RELEASE)/__/__/engine/ecode.o

$(OBJDIR_RELEASE)/__/__/engine/eir.o: ../../codegen/eir.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/eir.cpp -o $(OBJDIR_RELEASE)/__/__/engine/eir.o

$(OBJDIR_RELEASE)/__/__/engine/x86abi.o: ../../codegen/x86/abi.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/x86/abi.cpp -o $(OBJDIR_RELEASE)/__/__/engine/x86abi.o

$(OBJDIR_RELEASE)/__/__/engine/x86machine.o: ../../codegen/x86/machine.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/x86/machine.cpp -o $(OBJDIR_RELEASE)/__/__/engine/x86machine.o

$(OBJDIR_RELEASE)/__/__/engine/x86lowering.o: ../../codegen/x86/lowering.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/x86/lowering.cpp -o $(OBJDIR_RELEASE)/__/__/engine/x86lowering.o

$(OBJDIR_RELEASE)/__/__/engine/x86encoder.o: ../../codegen/x86/encoder.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/x86/encoder.cpp -o $(OBJDIR_RELEASE)/__/__/engine/x86encoder.o

$(OBJDIR_RELEASE)/__/__/engine/x86runtimecore.o: ../../codegen/x86/runtimecore.cpp ../../codegen/x86/runtimecore.h ../../codegen/runtimecore.h
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/x86/runtimecore.cpp -o $(OBJDIR_RELEASE)/__/__/engine/x86runtimecore.o

$(OBJDIR_RELEASE)/__/__/engine/x86runtimecore_x86.o: ../../codegen/x86/runtimecore_x86.cpp ../../codegen/x86/runtimecore.h ../../codegen/runtimecore.h
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/x86/runtimecore_x86.cpp -o $(OBJDIR_RELEASE)/__/__/engine/x86runtimecore_x86.o

$(OBJDIR_RELEASE)/__/__/engine/x86runtimecore_amd64.o: ../../codegen/x86/runtimecore_amd64.cpp ../../codegen/x86/runtimecore.h ../../codegen/runtimecore.h
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../codegen/x86/runtimecore_amd64.cpp -o $(OBJDIR_RELEASE)/__/__/engine/x86runtimecore_amd64.o

$(OBJDIR_RELEASE)/__/__/engine/bytecode.o: ../../engine/bytecode.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/bytecode.cpp -o $(OBJDIR_RELEASE)/__/__/engine/bytecode.o

$(OBJDIR_RELEASE)/__/__/engine/xmlprojectbase.o: ../../engine/xmlprojectbase.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/xmlprojectbase.cpp -o $(OBJDIR_RELEASE)/__/__/engine/xmlprojectbase.o

$(OBJDIR_RELEASE)/__/__/engine/serializer.o: ../../engine/serializer.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/serializer.cpp -o $(OBJDIR_RELEASE)/__/__/engine/serializer.o

$(OBJDIR_RELEASE)/__/__/engine/scriptreader.o: ../../engine/scriptreader.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/scriptreader.cpp -o $(OBJDIR_RELEASE)/__/__/engine/scriptreader.o

$(OBJDIR_RELEASE)/__/codeimage.o: ../codeimage.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../codeimage.cpp -o $(OBJDIR_RELEASE)/__/codeimage.o

$(OBJDIR_RELEASE)/__/compiler.o: ../compiler.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../compiler.cpp -o $(OBJDIR_RELEASE)/__/compiler.o

$(OBJDIR_RELEASE)/__/compiling.o: ../compiling.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../compiling.cpp -o $(OBJDIR_RELEASE)/__/compiling.o

$(OBJDIR_RELEASE)/__/derivation.o: ../derivation.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../derivation.cpp -o $(OBJDIR_RELEASE)/__/derivation.o

$(OBJDIR_RELEASE)/__/compilerlogic.o: ../compilerlogic.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../compilerlogic.cpp -o $(OBJDIR_RELEASE)/__/compilerlogic.o

$(OBJDIR_RELEASE)/__/modulescope.o: ../modulescope.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../modulescope.cpp -o $(OBJDIR_RELEASE)/__/modulescope.o

$(OBJDIR_RELEASE)/__/__/engine/linux/presenter.o: ../../engine/linux/presenter.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../../engine/linux/presenter.cpp -o $(OBJDIR_RELEASE)/__/__/engine/linux/presenter.o

$(OBJDIR_RELEASE)/__/linux/elc.o: ../linux/elc.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../linux/elc.cpp -o $(OBJDIR_RELEASE)/__/linux/elc.o

$(OBJDIR_RELEASE)/__/linux/elfimage.o: ../linux/elfimage.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../linux/elfimage.cpp -o $(OBJDIR_RELEASE)/__/linux/elfimage.o

$(OBJDIR_RELEASE)/__/linux/elflinker.o: ../linux/elflinker.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../linux/elflinker.cpp -o $(OBJDIR_RELEASE)/__/linux/elflinker.o

$(OBJDIR_RELEASE)/__/linux/elflinker32.o: ../linux/elflinker32.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../linux/elflinker32.cpp -o $(OBJDIR_RELEASE)/__/linux/elflinker32.o

$(OBJDIR_RELEASE)/__/linux/elflinker64.o: ../linux/elflinker64.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../linux/elflinker64.cpp -o $(OBJDIR_RELEASE)/__/linux/elflinker64.o

$(OBJDIR_RELEASE)/__/linux/elfsyslibloader.o: ../linux/elfsyslibloader.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../linux/elfsyslibloader.cpp -o $(OBJDIR_RELEASE)/__/linux/elfsyslibloader.o

$(OBJDIR_RELEASE)/__/linux/pathmanager.o: ../linux/pathmanager.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../linux/pathmanager.cpp -o $(OBJDIR_RELEASE)/__/linux/pathmanager.o

$(OBJDIR_RELEASE)/__/parser.o: ../parser.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../parser.cpp -o $(OBJDIR_RELEASE)/__/parser.o

$(OBJDIR_RELEASE)/__/separser.o: ../separser.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../separser.cpp -o $(OBJDIR_RELEASE)/__/separser.o

$(OBJDIR_RELEASE)/__/project.o: ../project.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../project.cpp -o $(OBJDIR_RELEASE)/__/project.o

$(OBJDIR_RELEASE)/__/source.o: ../source.cpp
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../source.cpp -o $(OBJDIR_RELEASE)/__/source.o

$(OBJDIR_RELEASE)/__/cli.o: ../cli.cpp ../../engine/x86compiler.h ../../codegen/runtime.h ../../codegen/x86/abi.h ../../engine/x86_64compiler.h
	$(CXX) $(CFLAGS_RELEASE) $(INC_RELEASE) -c ../cli.cpp -o $(OBJDIR_RELEASE)/__/cli.o

clean_release: 
	rm -f $(OBJ_RELEASE) $(OUT_RELEASE)
	rm -rf $(OBJDIR_RELEASE)/__
	rm -rf $(OBJDIR_RELEASE)/__/__/engine
	rm -rf $(OBJDIR_RELEASE)/__/linux
	rm -rf $(OBJDIR_RELEASE)/__/__/common

.PHONY: before_release after_release clean_release
