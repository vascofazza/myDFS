all : server test clean
server : libmyDFSCommons.a
	gcc -I myDFSCommons/ myDFSServer/server.c -o dfs_server -L . -lmyDFSCommons -lws2_32
libmyDFSCommons.a : mydfs_commons.o configuration_manager.o poll_helper.o read_utilities.o sem_utilities.o 
	ar rcs libmyDFSCommons.a myDFSCommons/*.o
libmyDFSClient.a : configuration_manager.o poll_helper.o read_utilities.o sem_utilities.o client.o
	ar rcs libmyDFSClient.a client.o myDFSCommons/*.o
client.o : libmyDFSCommons.a
	gcc -c -I myDFSCommons/ myDFSClient/client.c -o client.o -L myDFSCommons -lmyDFSCommons -lws2_32
mydfs_commons.o :
	gcc -c myDFSCommons/mydfs_commons.c -o myDFSCommons/mydfs_commons.o -lws2_32
configuration_manager.o :
	gcc -c myDFSCommons/configuration_manager.c -o myDFSCommons/configuration_manager.o -lws2_32
poll_helper.o :
	gcc -c myDFSCommons/poll_helper.c -o myDFSCommons/poll_helper.o -lws2_32
read_utilities.o :
	gcc -c myDFSCommons/read_utilities.c -o myDFSCommons/read_utilities.o -lws2_32
sem_utilities.o :
	gcc -c myDFSCommons/sem_utilities.c -o myDFSCommons/sem_utilities.o -lws2_32
test : libmyDFSClient.a
	gcc -I myDFSClient/ -I myDFSCommons/ test/main.c -o client_test -L . -lmyDFSClient -lws2_32
clean :
	del myDFSCommons\*.o
	del client.o