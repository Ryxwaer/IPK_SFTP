#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/statvfs.h>

#define MAXDATASIZE 1000

#define BACKLOG 10	 // how many pending connections queue will hold

long GetAvailableSpace(const char* path)
{
    struct statvfs stat;
    printf("path: %s\n", path);
    if (statvfs(path, &stat) != 0) {
        return -1;
    }

    // the available size is f_bsize * f_bavail
    return stat.f_bsize * stat.f_bavail;
}

void sigchld_handler(int s)
{
	(void)s; // quiet unused variable warning

	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}

// chceck if username exists in database
const char* getUser(char username[100], char filePath[PATH_MAX], char password[100])
{
    // if PASS is called before USER
    if (!username && password) {
        return "-send USER first";
    }
    FILE * fp;
    char * line = NULL;
    size_t len = 0;
    ssize_t read;

    char delim[] = ":";
    // open file with user database
    fp = fopen(filePath, "r");
    if (fp == NULL) return "-unable to access database, server error";
    // read file line by line
    while ((read = getline(&line, &len, fp)) != -1) {
        char *ptr = strtok(line, delim);
        // username found
        if (strcmp(ptr, username) == 0) {
            if (password) { // if command PASS was called
                ptr = strtok(NULL, "\n");
                if (strcmp(ptr, password) == 0) { // password found
                    fclose(fp);
                    if (line) free(line);
                    return "! Logged in";
                }
                fclose(fp);
                if (line) free(line);
                return "-Wrong password, try again";
            }
            fclose(fp);
            if (line) free(line);
            return "+User-id valid, send password";
        }
    }
    fclose(fp);
    if (line) free(line);

    if (password) {
        return "-Wrong password, try again";
    }
	return "-Invalid user-id, try again";
}

// function to concatenate two local path and chceck if it is valid and return absolute path
//
// wd = first local path       (example: "/Server/")
// path = second local path    (expmple: "test.txt")
// folder = { 0 | 1 } check if it is folder or file
//
// returns "error" if path is not valid, or absolute path to the folder / file
//
const char* concatPath(char wd[], char path[], char folder) {
    char *newPath = malloc (sizeof (char) * PATH_MAX);
    char *tmpOne = malloc (sizeof (char) * PATH_MAX);
    char *tmpTwo = malloc (sizeof (char) * PATH_MAX);
    char cwd[PATH_MAX];
    // get current working directory
    getcwd(cwd, sizeof(cwd));
    // path: "/currentWorkingDirectory/"
    strcpy(newPath, cwd);
    
    if (wd == NULL || !strcmp(wd, "/")) {
        strcpy(tmpOne, "");
    } else strcpy(tmpOne, wd);

    if (path == NULL || !strcmp(path, "/")) {
        strcpy(tmpTwo, "");
    } else strcpy(tmpTwo, path);

    if (tmpOne[0] != '/') {
        strcat(newPath, "/");
    }
    // path: "/currentWorkingDirectory/wd/"
    strcat(newPath, tmpOne);
    if (newPath[strlen(newPath)-1] == '/'){
        newPath[strlen(newPath)-1] = '\0';
    }
    if (tmpTwo[0] != '/') {
        strcat(newPath, "/");
    }
    // path: "/currentWorkingDirectory/wd/path"
    strcat(newPath, tmpTwo);
    if (newPath[strlen(newPath)-1] == '/'){
        newPath[strlen(newPath)-1] = '\0';
    }
    free(tmpOne);
    free(tmpTwo);
    printf("concatPath: %s\n", newPath);
    // chceck if it is valid folder
    if (folder && opendir (newPath) != NULL) {
        return(newPath);
    }
    else if (!folder && access( newPath, F_OK ) == 0) { // check if it is valid file
        return(newPath);
    }
    else return("error"); // file / folder not found
}

// function to get list of files / folders in specified directory
//
// path = absolute path to the requested directory
// verbose = { 1 | 0 } return verbose list or basic
//
const char* getList(char path[PATH_MAX], char verbose) {
    if (path == NULL) {
        return "-invalid input";
    }
    DIR *dir;
    struct dirent *ent;
    char *message = malloc (sizeof (char) * MAXDATASIZE);
    char buf[MAXDATASIZE];
    char cwd[PATH_MAX];
    // get current working directory
    getcwd(cwd, sizeof(cwd));
    // start to build message with list of files
    strcpy(message, "+");
    // if there is folder and we are able to access it
    if ((dir = opendir (path)) != NULL) {
        strcat(message, path + strlen(cwd)); // hide path to the server
        strcat(message, "\r\n"); // RFC913 sprecifies CRLF
        while ((ent = readdir (dir)) != NULL) {
            if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, "..")) { // ignore "." and ".."
                if (verbose) { // if verbose is required add type to the list
                    if (ent->d_type == DT_DIR) {
                        strcat(message, "directory :");
                    }
                    else if(ent->d_type == DT_REG) {
                        strcat(message, "file      :");
                    }
                    else {
                        strcat(message, "unknown   :");
                    }
                }
                strcat(message, ent->d_name); // add name to the list
                strcat(message, "\r\n");
            }
        }
        closedir (dir);
    } else {
        return "-file not found, try different path";
    }
    return message;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;

    char interface[100];
    char port[10] = "115";
    char user[PATH_MAX];
    char workingDir[PATH_MAX];
    char type = 'B';

    // help
    if (argc < 3)
    {
        puts("Usage: ./server -u /tmp/userpass.txt -f /tmp/simpleftp/ (-p 115) (-i eth0)\n");
        return -1;
    }

    // parse user arguments
    int i = 1;
    while(i < argc) {
        if (strcmp(argv[i], "-u") == 0) {
            i++;
            strcpy(user, argv[i]);
            printf("user = %s\n", user);
        } else if (strcmp(argv[i], "-f") == 0) {
            i++;
            strcpy(workingDir, argv[i]);
            printf("workingDir = %s\n", workingDir);
        } else if (strcmp(argv[i], "-p") == 0) {
            i++;
            strcpy(port, argv[i]);
            printf("port = %s\n", port);
        } else if (strcmp(argv[i], "-i") == 0) {
            i++;
            strcpy(interface, argv[i]);
            printf("interface = %s\n", interface);
        }
        i++;
    }

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // ipv6 / ipv4
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}
        if (interface) {
            if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, interface, sizeof(int)) == -1) {
                perror("setsockopt");
                exit(1);
            }
        }
		else {
            if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
                perror("setsockopt");
                exit(1);
            }
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}
		break;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
		printf("server: got connection from %s\n", s);

		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener

            char buf[MAXDATASIZE];
            int numbytes;
            char *userArgs;
            char loginVerification = 0;
            char *host = (char *)calloc(1024, sizeof(char));
            char username[100];
            char fileToRename[PATH_MAX];
            char fileToRetr[PATH_MAX];
            char fileToRecv[PATH_MAX];
            char fileToBeCreated[PATH_MAX];
            char append = 0;

            gethostname(host, sizeof(host));
            sprintf(buf, "+Connected to %s", host);
			if (send(new_fd, buf, strlen(buf), 0) == -1) {
				perror("send");
                break;
            }
            printf("[%05d] ACK sent to %s\n", getpid(), host);

            while(1) {  //////////////////////////////////////// serve client //
                memset(buf, 0, sizeof(buf));

                if ((numbytes = recv(new_fd, buf, MAXDATASIZE-1, 0)) <= 0) {
                    printf("numbytes=%d\n", numbytes);
                    break;
                }
                printf("[%05d]Receive: %s\n", getpid(), buf);

                userArgs = strtok(buf, " \r\n\t");
                
                // DONE - end connection, client is leaving
                if (strcasecmp(userArgs, "DONE") == 0) {
                    sprintf(buf, "+%s closing connection", host);
                    if ((numbytes = send(new_fd, buf, strlen(buf), 0)) <= 0)
                    {
                        perror("send");
                        break;
                    }
                    printf("[%05d]Send: %s\n\n=== Client (%s) Exit ===\n", getpid(), buf, s);
                    free(host);
                    break;
                }
                else if (strcasecmp(userArgs, "USER") == 0) {

                    userArgs = strtok(NULL, " \r\n\t");  // get user-id
                    if (userArgs) {
                        char tmpUserName[100];
                        strcpy(tmpUserName, userArgs);
                        strcpy(buf, getUser(tmpUserName, user, NULL));
                        if (strncmp(buf, "+", 1) == 0) {
                            loginVerification = '0';
                            strcpy(username, tmpUserName); // save username to persistent variable
                            // username could be also used for ACCT billing in the future
                        }
                    }
                    else {
                        strcpy(buf, "-wrong USER input");
                    }
                }
                else if (strcasecmp(userArgs, "PASS") == 0) {

                    userArgs = strtok(NULL, " \r\n\t");  // get password
                    if (userArgs) {
                        strcpy(buf, getUser(username, user, userArgs));
                        if (strncmp(buf, "!", 1) == 0) {
                            // getUser returned "! Logged in" so we can remember this useer as authentificated
                            loginVerification = '1';
                        }
                    }
                    else {
                        // there is no need to change loginVerification variable
                        strcpy(buf, "-wrong PASS input");
                    }
                }
                else if (strcasecmp(userArgs, "LIST") == 0) {
                    // chceck if user is logged in
                    if (loginVerification) {
                        char filePath[PATH_MAX];

                        userArgs = strtok(NULL, " \r\n\t");  // get option
                        if (strcasecmp(userArgs, "F") == 0) {
                            userArgs = strtok(NULL, " \r\n\t");  // get path, simple

                            strcpy(filePath, concatPath(workingDir, userArgs, 1));
                            if (strcmp(filePath, "error")) {
                                strcpy(buf, getList(filePath, 0)); // all good return list
                            }
                            else {
                                // if concatPath returns error it means that it is not valid path
                                strcpy(buf, "-path not found");
                            }
                        }                    // verbose
                        else if (strcasecmp(userArgs, "V") == 0) {
                            userArgs = strtok(NULL, " \r\n\t");  // get path, simple
                            // lokal path from request to the absolute path
                            strcpy(filePath, concatPath(workingDir, userArgs, 1));
                            if (strcmp(filePath, "error")) {
                                strcpy(buf, getList(filePath, 1));
                            }
                            else {
                                // there is no directory or it can not be accessed
                                strcpy(buf, "-path not found");
                            }
                        }
                        else {
                            strcpy(buf, "-wrong LIST option");
                        }
                    }
                    else strcpy(buf, "-login needed for LIST command");
                }
                else if (strcasecmp(userArgs, "TYPE") == 0) {

                    if (loginVerification) {

                        userArgs = strtok(NULL, " \r\n\t");  // get option
                        if (strcasecmp(userArgs, "A")) {
                            strcpy(buf, "+Using Ascii mode");
                            type = 'A';
                        }
                        else if (strcasecmp(userArgs, "B")) {
                            strcpy(buf, "+Using Binary mode");
                            type = 'B';
                        }
                        else if (strcasecmp(userArgs, "C")) {
                            strcpy(buf, "+Using Continuous mode");
                            type = 'C';
                        }
                        else {
                            memset(buf, 0, sizeof(buf));
                            strcpy(buf, "-Type not valid");
                        }
                    }
                    else {
                        strcpy(buf, "-login needed for TYPE command");
                    }
                }
                else if (strcasecmp(userArgs, "ACCT") == 0) { // server does not support billing

                    if (loginVerification) {
                        strcpy(buf, "! Account valid, logged-in");
                    }
                    else {
                        strcpy(buf, "-there is no need for ACCT, just login");
                    }
                }
                else if (strcasecmp(userArgs, "CDIR") == 0) {
                    
                    if (loginVerification) {
                        userArgs = strtok(NULL, " \r\n\t");  // get path
                        char filePath[PATH_MAX];
                        // chceck if path is valid
                        strcpy(filePath, concatPath(userArgs, NULL, 1));
                        if (strcmp(filePath, "error") == 0) {
                            strcpy(buf, "-Can't connect to directory");
                        }
                        else {
                            memset(workingDir, 0, sizeof(workingDir));
                            // change working directory of server
                            strcpy(workingDir, userArgs);
                            sprintf(buf, "!Changed working dir to %s", workingDir);
                        }
                    }
                    else {
                        strcpy(buf, "-login first");
                    }
                }
                else if (strcasecmp(userArgs, "KILL") == 0) {

                    if (loginVerification) {

                        userArgs = strtok(NULL, " \r\n\t");  // get path
                        if (userArgs == NULL || !strcmp(userArgs, "") || !strcmp(userArgs, "/")) {
                            strcpy(buf, "-wrong file path");
                        }
                        else {
                            char filePath[PATH_MAX];
                            // get absolute path
                            strcpy(filePath, concatPath(workingDir, userArgs, 0));
                            if (strcmp(filePath, "error") == 0) {
                                strcpy(buf, "-file not found");
                            }
                            else {
                                // delete file
                                if (remove(filePath) == 0) {
                                    sprintf(buf, "+%s deleted", userArgs);
                                } else {
                                    strcpy(buf, "-unable to delete file");
                                }
                            }
                        }
                    }
                    else {
                        strcpy(buf, "-login first");
                    }
                }
                else if (strcasecmp(userArgs, "NAME") == 0) {

                    if (loginVerification) {

                        userArgs = strtok(NULL, " \r\n\t");  // get path
                        if (userArgs == NULL || !strcmp(userArgs, "") || !strcmp(userArgs, "/")) {
                            strcpy(buf, "-wrong file name");
                        }
                        else {
                            char filePath[PATH_MAX];
                            // get absolute path
                            strcpy(filePath, concatPath(workingDir, userArgs, 0));
                            if (strcmp(filePath, "error") == 0) {
                                sprintf(buf, "-Can't find %s", userArgs);
                            }
                            else {
                                // save file to rename to persistent variable
                                memset(fileToRename, 0, sizeof(fileToRename));
                                strcpy(fileToRename, filePath);
                                memset(buf, 0, sizeof(buf));
                                strcpy(buf, "+File exists");
                            }
                        }
                    }
                    else {
                        memset(buf, 0, sizeof(buf));
                        strcpy(buf, "-login first");
                    }
                }
                else if (strcasecmp(userArgs, "TOBE") == 0) {

                    if (loginVerification) {
                        // if NAME request was called
                        if (fileToRename) {
                            userArgs = strtok(NULL, " \r\n\t");  // get new name
                            if (userArgs == NULL || !strcmp(userArgs, "") || !strcmp(userArgs, "/")) {
                                strcpy(buf, "-wrong file name");
                            }
                            char newName[PATH_MAX];
                            char oldName[PATH_MAX];
                            char oldPath[PATH_MAX];
                            char newPath[PATH_MAX];
                            strcpy(oldPath, fileToRename);
                            strcpy(newName, userArgs);
                            char *ptr;
                            ptr = strtok(oldPath, "/");
                            // get old file name from the whole path
                            while(ptr != NULL) {
                                strcpy(oldName, ptr);
                                ptr = strtok(NULL, "/");
                            }
                            strcpy(oldPath, fileToRename);
                            // get just path without file name
                            oldPath[strlen(oldPath) - strlen(oldName)] = '\0';
                            strcpy(newPath, oldPath);
                            strcat(newPath, newName);
                            // rename file
                            int ret = rename(fileToRename, newPath);
                            if(ret == 0) {
                                sprintf(buf, "+%s renamed to %s", oldName, newName);
                            } else {
                                strcpy(buf, "-unable to rename file");
                            }
                        }
                        else {
                            strcpy(buf, "-send NAME first");
                        }
                    }
                    else {
                        strcpy(buf, "-login first");
                    }
                }
                else if (strcasecmp(userArgs, "RETR") == 0) {

                    if (loginVerification) {
                        userArgs = strtok(NULL, " \r\n\t");  // get path
                        if (userArgs == NULL || !strcmp(userArgs, "") || !strcmp(userArgs, "/")) {
                            strcpy(buf, "-wrong file name");
                        }
                        else {
                            char filePath[PATH_MAX];
                            struct stat sb; // file stat structure
                            strcpy(filePath, concatPath(workingDir, userArgs, 0));
                            // get file stat if file exists
                            if (!strcmp(filePath, "error") || stat(filePath, &sb) == -1) {
                                strcpy(buf, "-File doesn't exist");
                            }
                            else { // all good, file exists
                                sprintf(buf, "%d", (long long) sb.st_size);
                                memset(fileToRetr, 0, sizeof(fileToRetr));
                                strcpy(fileToRetr, filePath);
                            }
                        }
                    }
                    else {
                        strcpy(buf, "-login first");
                    }
                }
                else if (strcasecmp(userArgs, "STOP") == 0) {
                    // client has no space to store file
                    if (fileToRetr) {
                        memset(fileToRetr, 0, sizeof(fileToRetr));
                        strcpy(buf, "+ok, RETR aborted");
                    }
                    else {
                        strcpy(buf, "-There is no file RETR to STOP");
                    }
                }
                else if (strcasecmp(userArgs, "SEND") == 0) {
                    if (fileToRetr) {
                        // no need to chceck for loginVerification because RETR call done it
                        struct stat fileStat;
                        int f;
                        // open file
                        f = open(fileToRetr, O_RDONLY);
                        if (f == -1 || fstat(f, &fileStat) < 0)
                        {
                            fprintf(stderr, "[%05d]FATAL ERROR: FILE CAN NOT BE OPENED\n", getpid());
                            break;
                        }
                        else
                        {
                            // file size
                            long remainData = fileStat.st_size;
                            char sendBuffer[MAXDATASIZE];
                            unsigned readAmount;
                            // read from file and send it to cliend until whole file is sent
                            while ((readAmount = read(f, sendBuffer, MAXDATASIZE)) > 0) {
                                unsigned totalWritten = 0;
                                do {
                                    unsigned actualWritten;
                                    actualWritten = write(new_fd, sendBuffer + totalWritten, readAmount - totalWritten);
                                    if (actualWritten == -1) {
                                        break;
                                    }
                                    totalWritten += actualWritten;
                                } while (totalWritten < readAmount);
                            }
                            // nollify the buffer
                            bzero(sendBuffer, MAXDATASIZE);
                            fprintf(stdout, "[%05d]MSG: FILE SENT\n", getpid());
                        }
                    }
                    else {
                        // there is nothing to send
                        strcpy(buf, "-send RETR command first");
                    }
                }
                else if (strcasecmp(userArgs, "STOR") == 0) {
                    if (loginVerification) {
                        userArgs = strtok(NULL, " \r\n\t");  // get option
                        char filePath[PATH_MAX];
                        char *ptr;
                        FILE *file;
                        printf("option: %s\n", userArgs);

                        if (strcasecmp(userArgs, "NEW") == 0) { // create new or with new name
                            userArgs = strtok(NULL, " \r\n\t");  // get path
                            strcpy(filePath, userArgs);

                            printf("filepath: %s\n", filePath);

                            char fileName[PATH_MAX];
                            ptr = strtok(filePath, "/");
                            while(ptr != NULL) {    // get just file name if path is present
                                strcpy(fileName, ptr);
                                ptr = strtok(NULL, "/");
                            }
                            char newGenName[PATH_MAX];
                            char fileExtension[100];
                            char tmpExtension[100];
                            char wholeName[PATH_MAX];
                            strcpy(wholeName, fileName);
                            ptr = strtok(fileName, ".");
                            while(ptr != NULL) {    // get file extension
                                printf("0\n");
                                strcpy(tmpExtension, ptr);
                                ptr = strtok(NULL, ".");
                            }
                            if (strcmp(fileName, tmpExtension) == 0) { // if filename has no extension
                                printf("1\n");
                                strcpy(fileExtension, "");
                            }
                            else {
                                printf("2\n");
                                strcpy(fileExtension, ".");
                                strcat(fileExtension, tmpExtension);
                                fileName[strlen(fileName)-strlen(fileExtension)] = '\0'; // delete extension
                            }
                            printf("fileExtension: %s, fileName: %s\n", fileExtension, fileName);
                            if (strcmp(concatPath(workingDir, wholeName, 0), "error")) { // file exists
                                int num = 0;
                                while (strcmp(concatPath(workingDir, wholeName, 0), "error")) {
                                    num++;    // new name = old_name ({num}).extension
                                    sprintf(wholeName, "%s(%d)%s", fileName, num, fileExtension);
                                    printf("*** new name = %s\n", wholeName);
                                }
                                // add number to the new filename based on its original end
                                sprintf(newGenName, "%s(%d)%s", fileName, num, fileExtension);
                                strcpy(buf, "+File exists, will create new generation of file");
                            }
                            else { // file not found
                                sprintf(newGenName, "%s%s", fileName, fileExtension);
                                strcpy(buf, "+File does not exist, will create new file");
                            }
                            // save file name to persistent variable
                            memset(fileToBeCreated, 0, sizeof(fileToBeCreated));
                            strcpy(fileToBeCreated, newGenName);
                            printf("fileToBeCreated= %s\n", fileToBeCreated);
                            append = 0;
                        }
                        else if (strcasecmp(userArgs, "OLD") == 0) { // create new or rewrite old
                            userArgs = strtok(NULL, " \r\n\t");  // get path
                            strcpy(filePath, userArgs);
                            
                            char fileName[PATH_MAX];
                            ptr = strtok(filePath, "/");
                            while(ptr != NULL) {    // get just file name if path is present
                                strcpy(fileName, ptr);
                                ptr = strtok(NULL, "/");
                            }
                            if (strcmp(concatPath(workingDir, fileName, 0), "error")) {
                                strcpy(buf, "+Will write over old file");
                            }
                            else {
                                strcpy(buf, "+Will create new file");
                            }
                            memset(fileToBeCreated, 0, sizeof(fileToBeCreated));
                            strcpy(fileToBeCreated, fileName);
                            printf("fileToBeCreated= %s\n", fileToBeCreated);
                            append = 0;
                        }
                        else if (strcasecmp(userArgs, "APP") == 0) { // create new or append to old
                            append = 0;
                            userArgs = strtok(NULL, " \r\n\t");  // get path
                            strcpy(filePath, userArgs);
                            
                            char fileName[PATH_MAX];
                            ptr = strtok(filePath, "/");
                            while(ptr != NULL) {    // get just file name if path is present
                                strcpy(fileName, ptr);
                                ptr = strtok(NULL, "/");
                            }
                            if(strcmp(concatPath(workingDir, fileName, 0), "error")) {
                                // persistent variable controlling how to open file later { "w" | "a" }
                                append = 1;
                                strcpy(buf, "+Will append to file");    
                            }
                            else {
                                strcpy(buf, "+Will create file");
                            }
                            memset(fileToBeCreated, 0, sizeof(fileToBeCreated));
                            strcpy(fileToBeCreated, fileName);
                            printf("fileToBeCreated= %s\n", fileToBeCreated);
                        }
                        else {
                            strcpy(buf, "-STOR invalid option, chose from { NEW | OLD | APP }");
                        }
                    }
                }
                else if (strcasecmp(userArgs, "SIZE") == 0) {
                    if (fileToBeCreated) {
                        userArgs = strtok(NULL, " \r\n\t");  // get file size in bytes
                        long size = atol(userArgs);
                        printf("size= %ld\n", size);
                        if (size > 0) {
                            long space = GetAvailableSpace(concatPath(workingDir, NULL, 1));
                            printf("space= %ld\n", space);
                            if (space > size + 10) { // add 10b to size just to reserver some space for the system
                                char *ptr;
                                char filePath[PATH_MAX];
                                strcpy(filePath, concatPath(workingDir, NULL, 1)); // get absolute path
                                if (filePath[strlen(filePath)-1] != '/') {
                                    strcat(filePath, "/");
                                }
                                strcat(filePath, fileToBeCreated); // add filename to the absolute path
                                printf("filePath= %s\n", filePath);
                                FILE *file;
                                // {append} or { create_new | rewrite_old }
                                if (append) {
                                    file=fopen(filePath,"a");
                                }
                                else {
                                    file=fopen(filePath,"w");
                                }
                                if (!file) {
                                    strcpy(buf, "-Unable to create file");
                                }
                                else {
                                    strcpy(buf, "+ok, waiting for file");
                                    if ((numbytes = send(new_fd, buf, strlen(buf), 0)) <= 0) {
                                        perror("send");
                                        break;
                                    }
                                    // loop receive whole file
                                    while ((size > 0) && ((numbytes = recv(new_fd, buf, sizeof(buf), 0)) > 0))
                                    {
                                        fwrite(buf, sizeof(char), numbytes, file);
                                        size -= numbytes;
                                    }
                                    fclose(file);
                                    memset(buf, 0, sizeof(buf));
                                    sprintf(buf, "+Saved %s", fileToBeCreated);
                                    memset(fileToBeCreated, 0, sizeof(fileToBeCreated));
                                }
                            }
                            else {
                                strcpy(buf, "-Not enough room, don't send it");
                            }
                        }
                    }
                    else {
                        strcpy(buf, "-send RETR command first");
                    }
                }
                else {
                    // this should never happend if client is configured right
                    strcpy(buf, "-unknown command");
                }

                // send parsed response for any request
                if ((numbytes = send(new_fd, buf, strlen(buf), 0)) <= 0) {
                    perror("send");
                    break;
                }
                printf("[%05d]Send: %s\n", getpid(), buf);
            } ////////////////////////////////////////////////////////////////////
			close(new_fd);
			exit(0);
		}
		close(new_fd);  // parent doesn't need this
	}

	return 0;
}
