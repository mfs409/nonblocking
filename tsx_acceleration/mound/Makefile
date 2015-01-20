###############################################################################
# 
#  Copyright (c) 2011
#  Lehigh University
#  Computer Science and Engineering Department
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
# 
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
# 
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
# 
#     * Neither the name of the University of Rochester nor the names of its
#       contributors may be used to endorse or promote products derived from
#       this software without specific prior written permission.
# 
# 
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
#  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGE.

# pull in all global Make variable definitions
-include Makefile.inc

CXXFLAGS += -mrtm -fno-strict-aliasing -Wall -D_REENTRANT -MMD -ggdb -pthread

# specify paths.  everything goes into OBJDIR
OBJDIR    = ./obj
FILENAMES = list
SOURCES   = $(patsubst %, %.cpp, $(FILENAMES))
OFILES    = $(patsubst %, $(OBJDIR)/%.o, $(FILENAMES))
DEPS      = $(patsubst %, $(OBJDIR)/%.d, $(FILENAMES))
CONFIG    = ../config.pl
GOALS     = $(OBJDIR)/moundtest $(OBJDIR)/inserttest $(OBJDIR)/removetest $(OBJDIR)/perftest $(OBJDIR)/sizetest $(OBJDIR)/exalltest $(OBJDIR)/priotest

# build rules
.PHONY: all clean realclean info

all: info $(OBJDIR) $(GOALS)
	@echo $(GOALS) complete

Makefile.inc config.h:
	@echo
	@echo Creating default configuration.
	@echo To reconfigure, type 'make clean' and then run $(CONFIG)
	@echo
	./$(CONFIG) -Z

# convince 'make' not to worry if a .d is missing
$(DEPS):

# to keep the build lines comprehensible, we hide the actual CXX invocations.
# Printing this information first helps if we need to recreate the invocation
# manually
info:
	@echo "Building with CXXFLAGS=${CXXFLAGS}"
	@echo "and LDFLAGS=${LDFLAGS}"

# /clean/ deletes everything from the obj directory
clean:
	@rm -f $(OFILES) $(GOALS)

# /realclean/ also kills the directory and the dependencies
realclean:
	@rm -rf $(OBJDIR) config.h Makefile.inc

# build the directory in which the .o files will go
$(OBJDIR):
	mkdir -p $@

$(OBJDIR)/%: %.cpp
	@echo [${CXX}] $< "-->" $@
	@$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

-include $(OBJDIR)/moundtest.d
-include $(OBJDIR)/inserttest.d
-include $(OBJDIR)/removetest.d
-include $(OBJDIR)/perftest.d
-include $(OBJDIR)/sizetest.d
-include $(OBJDIR)/exalltest.d
-include $(OBJDIR)/priotest.d
