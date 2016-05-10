all : hoc2 lex.yy.o hocgrammar.tab.c hocgrammar.tab.h

hoc2 : hoc2.o lex.yy.o

lex.yy.o : lex.yy.c hocgrammar.tab.h

lex.yy.c : hoclexer.l
	flex hoclexer.l

hocgrammar.tab.c hocgrammar.tab.h : hocgrammar.y
	bison -d hocgrammar.y

clean :
	rm -f lex.yy.o lex.yy.c hocgrammar.tab.c hocgrammar.tab.h
