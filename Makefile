.DEFAULT_GOAL=alltargets
alltargets:
	make -f Makefile.pepper.x86_32
	make -f Makefile.pepper.x86_64

clean:
	make -f Makefile.pepper.x86_32 clean
	make -f Makefile.pepper.x86_64 clean
	find . -name "*.d" | xargs rm -f
