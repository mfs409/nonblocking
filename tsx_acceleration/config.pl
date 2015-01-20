#!/usr/bin/perl

# set defaults
($bits, $opt, $os, $cc, $cpu) = (1, 1, 1, 1, 1);

# revise guesses for OS and CPU
if ($^O =~ "solaris") { $os = 1; } else { $os = 2; }
if (`uname -m` =~ "sun") { $cpu = 2; } else { $cpu = 1; }

# simple interactive routine: takes 3 args: num options, default option, and
# some text.  Doesn't do much error checking...
sub ask {
    print $_[2];
    print "Selection [$_[1]] :> ";
    $ans = readline(*STDIN);
    if ($ans =~ /^\n/) { $ans = $_[1]; }
    $ans = int($ans);
    if ($ans > $_[0]) { $ans = $_[1]; }
    print "\n";
    return $ans;
}

# If "-Z" given to program, turn off interactive mode
$interactive = 1;
if ($#ARGV != 1) { if ($ARGV[0] =~ "-Z") { $interactive = 0; } }

# Interactive mode
if ($interactive == 1) {
    $bits = ask(2, $bits, "What integer width?\n  [1] 32-bits\n  [2] 64-bits\n");
    $opt  = ask(2, $opt,  "What optimization level?\n  [1] O3\n  [2] O0\n");
    $os   = ask(2, $os,   "Which OS?\n  [1] Solaris\n  [2] Linux\n");
    $cc   = ask(2, $cc,   "Which compiler?\n  [1] gcc\n  [2] suncc\n");
    $cpu  = ask(2, $cpu,  "Which CPU?\n  [1] x86\n  [2] SPARC\n");
}

# dump to Makefile.inc
open(M, ">../Makefile.inc");
# do CXXFLAGS first
if ($opt == 1)  { print M "CXXFLAGS += -O3\n"; }
if ($opt == 2)  { print M "CXXFLAGS += -O0\n"; }
if ($bits == 1) { print M "CXXFLAGS += -m32\n" }
if ($bits == 2) { print M "CXXFLAGS += -m64\n" }
if ($cpu == 1)  { print M "CXXFLAGS += -msse2 -mfpmath=sse -march=native -mtune=native\nSUFFIX = x86\n" }
if ($cpu == 2)  { print M "CXXFLAGS += -pthreads -mcpu=v9 -Wno-deprecated\nSUFFIX = sparc\n" }
# then do LDFLAGS
if ($bits == 1) { print M "LDFLAGS += -m32\n" }
if ($bits == 2) { print M "LDFLAGS += -m64\n" }
if ($cpu == 1)  { print M "LDFLAGS += -lpthread -lrt\n" }
if ($cpu == 2)  { print M "LDFLAGS += -lpthreads -lrt\n" }
if ($os == 1)   { print M "LDFLAGS += -lmtmalloc\n" }
close M;

# dump to config.h
open(C, ">../config.h");
if ($bits == 1) { print C "#define STM_BITS_32\n"; }
if ($bits == 2) { print C "#define STM_BITS_64\n"; }
if ($opt == 1)  { print C "#define STM_OPT_O3\n"; }
if ($opt == 2)  { print C "#define STM_OPT_O0\n"; }
if ($os == 1)   { print C "#define STM_OS_SOLARIS\n"; }
if ($os == 2)   { print C "#define STM_OS_LINUX\n"; }
if ($cc == 1)   { print C "#define STM_CC_GCC\n"; }
if ($cc == 2)   { print C "#define STM_CC_SUNCC\n"; }
if ($cpu == 1)  { print C "#define STM_CPU_X86\n"; }
if ($cpu == 2)  { print C "#define STM_CPU_SPARC\n"; }
close C;
