make
truncate -s 10M image
./mkfs.a1fs -i 128 image -f
./a1fs ./image /tmp/wangh278
cd /tmp/wangh278
ls
touch ./file1
mkdir ./dir1
cd ./dir1
mkdir ./subdir1
cd ..
echo hello world! > ./file2
ls -lsa
cat ./file2
echo this assignment is really hard! >> ./file2
cat ./file2 > ./file3
cat ./file3
ls -lsa
rm -f ./file3
cd ..
fusermount -u /tmp/wangh278

