all : FORCE
	$(MAKE) -C cv_cpp
	$(MAKE) -C cv_posix

clean :
	$(MAKE) -C cv_cpp clean	
	$(MAKE) -C cv_posix clean
