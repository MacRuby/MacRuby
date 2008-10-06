provider macruby {
    probe insn__entry(char *insnname, char *sourcefile, int sourceline);
    probe insn__return(char *insnname, char *sourcefile, int sourceline);
    probe method__entry(char *classname, char *methodname, char *sourcefile, int sourceline);
    probe method__return(char *classname, char *methodname, char *sourcefile, int sourceline);
    probe raise(char *classname, char *sourcefile, int sourceline);
    probe rescue(char *sourcefile, int sourceline);
};
