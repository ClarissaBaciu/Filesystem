ECSE427 & COMP 310 : File System on Simulated Disk on Linux

Creation of a 1 level file system containing functions to : get next file name, get file size, open a file, close a file, write to a file, read from a file, move the pointer to the beginning of the file, and remove a file. Implemented using directory tables, free bit maps, file allocation tables, inode tables and memory blocks.  


All relevant code is included in sfs.c, function declarations are in the header file sfs_api.h.

Tests: 

- Must add '-lm' flag for floor function. 

- Attempted to modify makefile appropriately but it was not working so I simply added a
 “#include "sfs.c” on top of the test files and ran the min terminal with ‘gcc <testfile.c> -lm'

-  All tests are passing completely except for 6 errors in sfs_test2.c

- Note : Test 2 has an undeclared variable MAXFILENAME which I replaced with
 MAX_FNAME_LENGTH, since it was declared in the test file and i believe it performs the same function. 

- Test outputs can be found below if there are issues running the code 


TEST 0 OUTPUT : 

The quick brown fox jumps over the lazy dog

TEST 3 OUTPUT : 


Closed file successfully
sfs_getfilesize test passed.
Closed file successfully
sfs_getfilesize test passed
Read from a closed file test passed
Created 3 files in the root directory
Simultaneously opened 3 files
Directory listing
Succesfully passed the sfs_getnextfilename tests


TEST 1 OUTPUT : 

Two files created with zero length:
File CXAVDFEJDZBQKRXQ.YBANYERFMFFPJZO now has length 26091 and WQLZWPKBRNRBERTE.UTRSZKYNQFCBFTR now has length 19163:
Created 20 files in the root directory
Simultaneously opened 20 files
ok
Trying to fill up the disk with repeated writes to ZGDIASTMXXAHSIVJ.KETMYDEITZWPOAN.
(This may take a while).
Write failed after 267 iterations.
If the emulated disk contains just over 273408 bytes, this is OK
Test program exiting with 0 errors


TEST 2 OUTPUT : 

Two files created with zero length:
File OCRCUPGGPDGTZCVB.KTZAZFZUORBVCG now has length 26154 and RIBVFSBNHETAGWVH.IRILQOKMCBNZFU now has length 26057:
Created 100 files in the root directory
Simultaneously opened 100 files
Trying to fill up the disk with repeated writes to IFWUAIBXJQDDGOWT.HZXKLRFJZCIVYO.
(This may take a while).
Write failed after 267 iterations.
If the emulated disk contains just over 273408 bytes, this is OK
Directory listing
ERROR: Wrong byte in AUUVGXRADNFZNNCA.SXWXSGODTPOIBX at position 0 (5,84)
ERROR: Wrong byte in YBRSWXRQZWFEVSTX.VNXTLPABUTSLET at position 0 (6,84)
ERROR: Wrong byte in ICWBWTANLZJQFGKA.GHODBBUDEOYWZC at position 0 (7,84)
ERROR: Wrong byte in SJGOKCJMRULCMSLZ.VRGJUJMQMQGNMI at position 0 (8,84)
ERROR: Wrong byte in RGRYVECEQWZEANWL.MRCVCYGOOVGVIV at position 0 (9,84)
ERROR: Wrong byte in DZBUZWYEBRACVAPR.MELOBPNHGDCMYK at position 0 (10,84)
Test program exiting with 6 errors

