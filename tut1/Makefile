#NOTE: You will need to change this path if you run this on your own machine.
# You can do so without modifying this makefile by running:
# 	IMAGE_PATH=<your path> make tests
IMAGE_PATH ?= /u/csc369h/winter/pub/diskimages

readimage: readimage.c ext2.h
	gcc -Wall -g -o readimage readimage.c

# If the tests are successful, you will see no output
# If one tests fails the remaining tests will not run
tests: readimage
	readimage -t ${IMAGE_PATH}/emptydisk.img | diff -w emptydisk.expected -
	readimage -t ${IMAGE_PATH}/twolevel.img  | diff -w twolevel.expected  -
	readimage -t ${IMAGE_PATH}/largefile.img | diff -w largefile.expected -

clean:
	rm -f readimage
