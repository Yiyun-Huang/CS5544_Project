CXX          = clang++
CXXFLAGS     = -rdynamic $(shell llvm-config --cxxflags) -fPIC -g -std=c++20
LDFLAGS      = $(shell llvm-config --ldflags | tr '\n' ' ') -Wl,--exclude-libs,ALL
BUILDDIR     = build
DEPDIR       = $(BUILDDIR)/.deps
DEPFLAGS     = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

OPTIMIZER_SOURCES = unifiedpass.cpp
OPTIMIZER_LIBS    = $(OPTIMIZER_SOURCES:%.cpp=$(BUILDDIR)/%.so)
DEPFILES          = $(OPTIMIZER_SOURCES:%.cpp=$(DEPDIR)/%.d)

.PHONY: all clean 

all: $(OPTIMIZER_LIBS)

clean:
	rm -rf $(BUILDDIR)

$(BUILDDIR)/%.o: %.cpp $(DEPDIR)/%.d | $(DEPDIR) $(BUILDDIR)
	$(CXX) $(DEPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/%.so: $(BUILDDIR)/%.o
	$(CXX) -shared $^ -o $@ $(LDFLAGS)

$(DEPFILES):
-include $(wildcard $(DEPFILES))