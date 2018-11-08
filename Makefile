all : FORCE
	$(MAKE) -C cpp
	$(MAKE) -C posix

clean :
	$(MAKE) -C cpp clean	
	$(MAKE) -C posix clean

FORCE:
