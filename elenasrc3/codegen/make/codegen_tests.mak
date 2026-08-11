CXX = g++
CXXFLAGS = -Wall -Wextra -Werror -Wno-type-limits -Wno-tautological-constant-out-of-range-compare -std=c++20 -O2 -I.. -I../../common
OBJDIR = ../../temp/codegen-tests
OUT = ../../../bin/codegen-tests
OBJECTS = $(OBJDIR)/target.o $(OBJDIR)/runtime.o $(OBJDIR)/runtimecore.o $(OBJDIR)/dispatch.o $(OBJDIR)/method.o $(OBJDIR)/ecode.o $(OBJDIR)/eir.o $(OBJDIR)/x86_abi.o $(OBJDIR)/x86_machine.o $(OBJDIR)/x86_lowering.o $(OBJDIR)/x86_encoder.o $(OBJDIR)/x86_runtimecore.o $(OBJDIR)/x86_runtimecore_x86.o $(OBJDIR)/x86_runtimecore_amd64.o $(OBJDIR)/tests.o
export CCACHE_DISABLE = 1

all: $(OUT)

$(OUT): $(OBJECTS)
	$(CXX) -o $(OUT) $(OBJECTS)

$(OBJDIR)/target.o: ../target.cpp ../target.h
	mkdir -p $(OBJDIR) ../../../bin
	$(CXX) $(CXXFLAGS) -c ../target.cpp -o $(OBJDIR)/target.o

$(OBJDIR)/runtime.o: ../runtime.cpp ../runtime.h ../target.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../runtime.cpp -o $(OBJDIR)/runtime.o

$(OBJDIR)/runtimecore.o: ../runtimecore.cpp ../runtimecore.h ../runtime.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../runtimecore.cpp -o $(OBJDIR)/runtimecore.o

$(OBJDIR)/dispatch.o: ../dispatch.cpp ../dispatch.h ../../engine/bytecode.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../dispatch.cpp -o $(OBJDIR)/dispatch.o

$(OBJDIR)/method.o: ../method.cpp ../method.h ../../engine/bytecode.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../method.cpp -o $(OBJDIR)/method.o

$(OBJDIR)/ecode.o: ../ecode.cpp ../ecode.h ../dispatch.h ../../engine/bytecode.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../ecode.cpp -o $(OBJDIR)/ecode.o

$(OBJDIR)/eir.o: ../eir.cpp ../eir.h ../dispatch.h ../method.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../eir.cpp -o $(OBJDIR)/eir.o

$(OBJDIR)/x86_abi.o: ../x86/abi.cpp ../x86/abi.h ../x86/isa.h ../target.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../x86/abi.cpp -o $(OBJDIR)/x86_abi.o

$(OBJDIR)/x86_machine.o: ../x86/machine.cpp ../x86/machine.h ../x86/abi.h ../x86/isa.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../x86/machine.cpp -o $(OBJDIR)/x86_machine.o

$(OBJDIR)/x86_lowering.o: ../x86/lowering.cpp ../x86/lowering.h ../x86/machine.h ../ecode.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../x86/lowering.cpp -o $(OBJDIR)/x86_lowering.o

$(OBJDIR)/x86_encoder.o: ../x86/encoder.cpp ../x86/encoder.h ../x86/machine.h ../target.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../x86/encoder.cpp -o $(OBJDIR)/x86_encoder.o

$(OBJDIR)/x86_runtimecore.o: ../x86/runtimecore.cpp ../x86/runtimecore.h ../runtimecore.h ../runtime.h ../x86/abi.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../x86/runtimecore.cpp -o $(OBJDIR)/x86_runtimecore.o

$(OBJDIR)/x86_runtimecore_x86.o: ../x86/runtimecore_x86.cpp ../x86/runtimecore.h ../runtimecore.h ../runtime.h ../x86/abi.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../x86/runtimecore_x86.cpp -o $(OBJDIR)/x86_runtimecore_x86.o

$(OBJDIR)/x86_runtimecore_amd64.o: ../x86/runtimecore_amd64.cpp ../x86/runtimecore.h ../runtimecore.h ../runtime.h ../x86/abi.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../x86/runtimecore_amd64.cpp -o $(OBJDIR)/x86_runtimecore_amd64.o

$(OBJDIR)/tests.o: ../tests/tests.cpp ../ecode.h ../eir.h ../runtime.h ../runtimecore.h ../target.h ../x86/encoder.h ../x86/lowering.h ../x86/runtimecore.h
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c ../tests/tests.cpp -o $(OBJDIR)/tests.o

test: $(OUT)
	$(OUT)

clean:
	rm -f $(OBJECTS) $(OUT)

.PHONY: all test clean
