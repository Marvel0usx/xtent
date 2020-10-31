truncate -s 10M image
./mkfs.a1fs -i 128 image
mkdir test_root
./a1fs image ./test_root

cd ./test_root
ls
touch file1
mkdir dir1
cd dir1
mkdir subdir1
cd ..
echo hello world! > file2
ls -lsa
cat file2
echo this assignment is really hard! >> file2
cat file2 > file3
cat file3
ls -lsa
rm -f file3
cd ..
fusermount -u ./test_root

./a1fs image ./test_root
rm -rf .
ls -lsa
cd ..
fusermount -u ./test_root

