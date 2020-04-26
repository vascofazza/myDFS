all : server test clean
server : libmyDFSCommons.a
	gcc -pthread -I myDFSCommons/ myDFSServer/server.c -o dfs_server -L . -lmyDFSCommons
libmyDFSCommons.a : mydfs_commons.o configuration_manager.o poll_helper.o read_utilities.o sem_utilities.o 
	ar rcs libmyDFSCommons.a myDFSCommons/*.o
libmyDFSClient.a : configuration_manager.o poll_helper.o read_utilities.o sem_utilities.o client.o
	ar rcs libmyDFSClient.a client.o myDFSCommons/*.o
client.o : libmyDFSCommons.a
	gcc -pthread -c -I myDFSCommons/ myDFSClient/client.c -o client.o -L myDFSCommons -lmyDFSCommons
mydfs_commons.o :
	gcc -c myDFSCommons/mydfs_commons.c -o myDFSCommons/mydfs_commons.o
configuration_manager.o :
	gcc -c myDFSCommons/configuration_manager.c -o myDFSCommons/configuration_manager.o
poll_helper.o :
	gcc -c myDFSCommons/poll_helper.c -o myDFSCommons/poll_helper.o
read_utilities.o :
	gcc -c myDFSCommons/read_utilities.c -o myDFSCommons/read_utilities.o	
sem_utilities.o :
	gcc -c myDFSCommons/sem_utilities.c -o myDFSCommons/sem_utilities.o
test : libmyDFSClient.a
	gcc -pthread -I myDFSClient/ -I myDFSCommons/ test/main.c -o client_test -L . -lmyDFSClient
clean :
	rm -f myDFSCommons/*.o
	rm -f client.o