all : FORCE
	$(MAKE) -C cv-cpp
	$(MAKE) -C cv-posix

clean :
	$(MAKE) -C cv-cpp clean	
	$(MAKE) -C cv-posix clean

FORCE:
