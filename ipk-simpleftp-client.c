#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <arpa/inet.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAXDATASIZE 1000 // max number of bytes we can get at once 

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

long GetAvailableSpace(const char* path)
{
    struct statvfs stat;
    if (statvfs(path, &stat) != 0) {
        return -1;
    }

    // the available size is f_bsize * f_bavail
    return stat.f_bsize * stat.f_bavail;
}

// function to concatenate two paths (absolute + local)
// returns absolute path without checking if is valid
const char* concatPath(char wd[], char path[]) {
    // create temporary variables
    char *newPath = malloc (sizeof (char) * PATH_MAX);
    char *tmpOne = malloc (sizeof (char) * PATH_MAX);
    char *tmpTwo = malloc (sizeof (char) * PATH_MAX);
    // first path
    strcpy(tmpOne, wd);
    // make it into correct format
    if (path == NULL || !strcmp(path, "/")) {
        strcpy(tmpTwo, "");
    } else strcpy(tmpTwo, path);
    if (tmpOne[0] != '/') {
        strcat(newPath, "/");
    }
    strcat(newPath, tmpOne);
    if (newPath[strlen(newPath)-1] == '/'){
        newPath[strlen(newPath)-1] = '\0';
    }
    if (tmpTwo[0] != '/') {
        strcat(newPath, "/");
    }
    // second path
    strcat(newPath, tmpTwo);
    if (newPath[strlen(newPath)-1] == '/'){
        newPath[strlen(newPath)-1] = '\0';
    }
    free(tmpOne);
    free(tmpTwo);
    return(newPath);
}

int main(int argc, char *argv[])
{
	int sockfd, numbytes;  
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

    char host[200];
    char port[10] = "115";
    char folder[PATH_MAX] = "/";
    char type = 0;
    char workingDir[PATH_MAX];

    // help
	if (argc < 3)
    {
        puts("Usage: ./client -h 192.168.1.1 -f /tmp/simpleftp/ (-p 115)\n");
        return -1;
    }
    
    // parse agruments
    int i = 1;
    while(i < argc) {
        if (strcmp(argv[i], "-f") == 0) {
            i++;
            strcpy(folder, argv[i]);
            printf("folder = %s\n", folder); // lokal path from script to working directory
        } else if (strcmp(argv[i], "-p") == 0) {
            i++;
            strcpy(port, argv[i]);
            printf("port = %s\n", port);
        } else if (strcmp(argv[i], "-h") == 0) {
            i++;
            strcpy(host, argv[i]);
            printf("host = %s\n", host);
        }
        i++;
    }
    // chceck working directory validity
    getcwd(workingDir, sizeof(workingDir)); // get current working directory
    strcpy(workingDir, concatPath(workingDir, folder));
    DIR* dir = opendir(workingDir);
    if (dir) {
        // Directory exists
        closedir(dir);
    } else if (ENOENT == errno) {
        printf("working directory <%s> does not exist", workingDir);
        exit(2);
    } else {
        printf("filed to open working directory <%s>", workingDir);
        exit(2);
    }
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // can be any ipv4 or ipv6
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("client: connect");
			close(sockfd);
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
	printf("client: connecting to %s\n", s);

	freeaddrinfo(servinfo); // all done with this structure

	if ((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) == -1) {
	    perror("recv");
	    exit(1);
	}

	buf[numbytes] = '\0';

    if (buf[0] == '+') {
        printf("Connected to server %s\n", host);
    }
    while (1) {
        memset(buf, 0, sizeof(buf));
        fprintf(stdout, ">SFTP Client: ");
        fgets(buf, sizeof(buf) / sizeof(char), stdin);
        buf[strcspn(buf, "\n")] = 0;

        if (!strlen(buf)) strcpy(buf, "NULL");

        if (strncasecmp(buf, "DONE", 4) == 0) { // DONE

            if ((numbytes = send(sockfd, "DONE", 4, 0)) <= 0)
            {
                perror("send");
                break;
            }
            if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0)
            {
                printf("numbytes=%d\n", numbytes);
            }
            printf(">SFTP Server: %s\n", buf);
            break;
        }
        else if (strncasecmp(buf, "USER", 4) == 0) {  // USER

            if ((numbytes = send(sockfd, buf, strlen(buf), 0)) <= 0)
            {
                perror("send");
                break;
            }
            if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0)
            {
                printf("recv error: numbytes=%d\n", numbytes);
            }
            printf(">SFTP Server: %s\n", buf);
        }
        else if (strncasecmp(buf, "PASS", 4) == 0) {  // PASS

            if ((numbytes = send(sockfd, buf, strlen(buf), 0)) <= 0)
            {
                perror("send");
                break;
            }
            if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0)
            {
                printf("recv error: numbytes=%d\n", numbytes);
            }
            printf(">SFTP Server: %s\n", buf);
        }
        else if (strncasecmp(buf, "LIST", 4) == 0) {  // LIST

            if ((numbytes = send(sockfd, buf, strlen(buf), 0)) <= 0)
            {
                perror("send");
                break;
            }
            if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0)
            {
                printf("recv error: numbytes=%d\n", numbytes);
            }
            printf(">SFTP Server: %s\n", buf);
        }
        else if (strncasecmp(buf, "TYPE", 4) == 0) {  // TYPE
            if (buf[6]) {
                type = buf[6];
            }
            if ((numbytes = send(sockfd, buf, strlen(buf), 0)) <= 0)
            {
                perror("send");
                break;
            }
            if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0)
            {
                printf("recv error: numbytes=%d\n", numbytes);
            }
            if (buf[0] == '-') {
                type = '0';
            }
            printf(">SFTP Server: %s\n", buf);
        }
        else if (strncasecmp(buf, "ACCT", 4) == 0) {  // ACCT

            if ((numbytes = send(sockfd, buf, strlen(buf), 0)) <= 0)
            {
                perror("send");
                break;
            }
            if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0)
            {
                printf("recv error: numbytes=%d\n", numbytes);
            }
            printf(">SFTP Server: %s\n", buf);
        }
        else if (strncasecmp(buf, "CDIR", 4) == 0) {  // CDIR

            if ((numbytes = send(sockfd, buf, strlen(buf), 0)) <= 0)
            {
                perror("send");
                break;
            }
            if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0)
            {
                printf("recv error: numbytes=%d\n", numbytes);
            }
            printf(">SFTP Server: %s\n", buf);
        }
        else if (strncasecmp(buf, "KILL", 4) == 0) {  // KILL

            if ((numbytes = send(sockfd, buf, strlen(buf), 0)) <= 0) {
                perror("send");
                break;
            }
            if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0) {
                printf("recv error: numbytes=%d\n", numbytes);
            }
            printf(">SFTP Server: %s\n", buf);
        }
        else if (strncasecmp(buf, "NAME", 4) == 0) {  // NAME

            if ((numbytes = send(sockfd, buf, strlen(buf), 0)) <= 0) {
                perror("send");
                break;
            }
            memset(buf, 0, sizeof(buf));
            if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0) {
                printf("recv error: numbytes=%d\n", numbytes);
            }
            printf(">SFTP Server: %s\n", buf);
        }
        else if (strncasecmp(buf, "TOBE", 4) == 0) {  // TOBE

            if ((numbytes = send(sockfd, buf, strlen(buf), 0)) <= 0) {
                perror("send");
                break;
            }
            memset(buf, 0, sizeof(buf));
            if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0) {
                printf("recv error: numbytes=%d\n", numbytes);
            }
            printf(">SFTP Server: %s\n", buf);
        }
        else if (strncasecmp(buf, "RETR", 4) == 0) {  // RETR
            char filePath[PATH_MAX];
            strcpy(filePath, buf + 5);
            if ((numbytes = send(sockfd, buf, strlen(buf), 0)) <= 0) {
                perror("send");
                break;
            }
            memset(buf, 0, sizeof(buf));
            if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0) {
                printf("recv error: numbytes=%d\n", numbytes);
            }
            printf(">SFTP Server: %s\n", buf);
            long size = atol(buf); // convert received bytes to long
            if (size > 0) {
                long space = GetAvailableSpace(workingDir);
                if (space > size + 10) { // add 10b to size just to reserver some space for the system
                    char *ptr;
                    char fileName[100];
                    ptr = strtok(filePath, "/");
                    while(ptr != NULL) {    // get just file name if it is path
                        strcpy(fileName, ptr);
                        ptr = strtok(NULL, "/");
                    }
                    strcpy(filePath, concatPath(workingDir, fileName));
                    FILE *file;
                    // create file to write received content into
                    if (!(file=fopen(filePath,"w"))) {
                        perror("unable to create file");
                        if ((numbytes = send(sockfd, "STOP", 4, 0)) <= 0) {
                            perror("send");
                            break;
                        }
                        if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0) {
                            printf("recv error: numbytes=%d\n", numbytes);
                        }
                    }
                    else if ((numbytes = send(sockfd, "SEND", 4, 0)) <= 0) { // SEND sent automatically if all good
                        perror("send");
                        break;
                    }
                    else { // receive file bytes in while loop until all are received and written 
                        while ((size > 0) && ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) > 0))
                        {
                            fwrite(buf, sizeof(char), numbytes, file);
                            size -= numbytes;
                        }
                        fclose(file);
                    }
                }
                else {
                    if ((numbytes = send(sockfd, "STOP", 4, 0)) <= 0) { // STOP sent automatically if error
                        perror("send");
                        break;
                    }
                    if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0) {
                        printf("recv error: numbytes=%d\n", numbytes);
                    }
                }
            }
        }
        else if (strncasecmp(buf, "STOR", 4) == 0) {  // STOR
            int f;
            struct stat fileStat;
            char filePath[PATH_MAX];
            char localpath[PATH_MAX];
            strcpy(localpath, buf +9);
            // every path must be inputed as local path, not absolute
            strcpy(filePath, concatPath(workingDir, localpath));
            f = open(filePath, O_RDONLY);
            // chceck if file exists and load file into structure for futher size request
            if (f == -1 || fstat(f, &fileStat) < 0) {
                printf("ERR: File %s can not by opened\n", filePath);
            }
            else {
                if ((numbytes = send(sockfd, buf, strlen(buf), 0)) <= 0) {
                    perror("send");
                    break;
                }
                memset(buf, 0, sizeof(buf));
                if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0) {
                    printf("numbytes=%d\n", numbytes);
                }
                printf(">SFTP Server: %s\n", buf);
                if (buf[0] == '+') { // will create the file
                    memset(buf, 0, sizeof(buf));
                    sprintf(buf, "SIZE %d", fileStat.st_size); // SIZE <bytes-size>
                    if ((numbytes = send(sockfd, buf, strlen(buf), 0)) <= 0) {
                        perror("send");
                        break;
                    }
                    if ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) <= 0) {
                        printf("recv error: numbytes=%d\n", numbytes);
                    }
                    if (buf[0] == '+') { // ok, waiting for file
                        long remainData = fileStat.st_size;
                        char sendBuffer[MAXDATASIZE];
                        unsigned readAmount;
                        // while loop until all bytes are read and sent
                        while ((readAmount = read(f, sendBuffer, MAXDATASIZE)) > 0) {
                            unsigned totalWritten = 0;
                            do {
                                unsigned actualWritten;
                                actualWritten = write(sockfd, sendBuffer + totalWritten, readAmount - totalWritten);
                                if (actualWritten == -1) {
                                    break;
                                }
                                totalWritten += actualWritten;
                            } while (totalWritten < readAmount);
                        }
                        // nullify buffer
                        bzero(sendBuffer, MAXDATASIZE);
                        printf("file sent\n", getpid());
                    }
                }
            }
        }
    }

	close(sockfd);

	return 0;
}
