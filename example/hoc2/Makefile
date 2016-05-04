all : lex.yy.o hocgrammar.tab.c hocgrammar.tab.h

lex.yy.o : lex.yy.c hocgrammar.tab.h
	gcc -c lex.yy.c

lex.yy.c : hoclexer.l
	flex hoclexer.l

hocgrammar.tab.c hocgrammar.tab.h : hocgrammar.y
	bison -d hocgrammar.y

clean : 
	rm -f lex.yy.o lex.yy.c hocgrammar.tab.c hocgrammar.tab.h
