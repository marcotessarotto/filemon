/*
 ============================================================================
 Name        : filemon.c
 Author      : 
 Version     :
 Copyright   : Marco Tessarotto (c) 2023
 Description : monitors one or more files or directories specified as parameters; when a new file is detected, invokes an action on it
 ============================================================================
 */

#include <sys/types.h>  /* Type definitions used by many programs */
#include <stdio.h>      /* Standard I/O functions */
#include <stdlib.h>     /* Prototypes of commonly used library functions,
                           plus EXIT_SUCCESS and EXIT_FAILURE constants */
#include <unistd.h>     /* Prototypes for many system calls */
#include <errno.h>      /* Declares errno and defines error constants */
#include <string.h>     /* Commonly used string-handling functions */
#include <stdbool.h>    /* 'bool' type plus 'true' and 'false' constants */

#include <sys/inotify.h>
#include <limits.h>

#include <sys/wait.h>


char * command = NULL;
#define MAX_COMMAND_LEN (PATH_MAX - NAME_MAX - 1)


char space[] = " ";

static void show_inotify_event(struct inotify_event *i)
{
    printf("[wd=%2d] ", i->wd);

    if (i->cookie > 0)
        printf("cookie=%4d ", i->cookie);

    // The  name  field is present only when an event is returned for a file
    // inside a watched directory; it identifies the filename within the watched directory.
    // This filename is null-terminated .....

    if (i->len > 0)
        printf("file name = %s ", i->name);
    else
    	printf("*no file name* "); // event refers to watched directory

    // see man inotify
    // for explanation of events

    printf("mask = ");

    // IN_ACCESS  File was accessed (e.g., read(2), execve(2)).
    if (i->mask & IN_ACCESS)        printf("IN_ACCESS ");

    // IN_ATTRIB Metadata changed—for example, permissions, timestamps, user/group ID
    if (i->mask & IN_ATTRIB)        printf("IN_ATTRIB ");

    if (i->mask & IN_CLOSE_NOWRITE) printf("IN_CLOSE_NOWRITE ");

    // IN_CLOSE_NOWRITE  File or directory not opened for writing was closed.
    if (i->mask & IN_CLOSE_WRITE)   printf("IN_CLOSE_WRITE ");

    if (i->mask & IN_CREATE)        printf("IN_CREATE ");
    if (i->mask & IN_DELETE)        printf("IN_DELETE ");
    if (i->mask & IN_DELETE_SELF)   printf("IN_DELETE_SELF ");
    if (i->mask & IN_IGNORED)       printf("IN_IGNORED ");

    // IN_ISDIR  Subject of this event is a directory.
    if (i->mask & IN_ISDIR)         printf("IN_ISDIR ");

    if (i->mask & IN_MODIFY)        printf("IN_MODIFY ");
    if (i->mask & IN_MOVE_SELF)     printf("IN_MOVE_SELF ");
    if (i->mask & IN_MOVED_FROM)    printf("IN_MOVED_FROM ");
    if (i->mask & IN_MOVED_TO)      printf("IN_MOVED_TO ");

    // IN_OPEN  File or directory was opened.
    if (i->mask & IN_OPEN)          printf("IN_OPEN ");

    if (i->mask & IN_Q_OVERFLOW)    printf("IN_Q_OVERFLOW ");
    if (i->mask & IN_UNMOUNT)       printf("IN_UNMOUNT ");
    printf("\n");


    // flags.CREATE | flags.CLOSE_WRITE | flags.CLOSE_NOWRITE

    if ((i->mask & IN_CREATE) || (i->mask & IN_CLOSE_WRITE) || (i->mask & IN_CLOSE_NOWRITE)) {
    	// execute command passing file as parameter
    	if (i->len) {

    		char cmd[PATH_MAX];
    		cmd[0] = 0;

    		// append command to cmd
    		strcat(cmd, command);

    		// append ' ' to cmd
    		strcat(cmd, space);

    		// append file name do cmd
    		strncat(cmd, i->name, i->len);

    		printf("cmd: %s\n", cmd);

    		pid_t child_pid;
    		int wstatus;
    		int modal_result = -1;

    		switch (child_pid = fork()) {
    		case -1:
    			perror("cannot fork");
    			exit(EXIT_FAILURE);
    		case 0:

    			child_pid = getpid();
    			printf("[child process] pid=%d\n", child_pid);

    			if (execl("/bin/sh", "sh", "-c", cmd, (char *) NULL) != 0) {
    				perror("[child process] execl");
    				exit(EXIT_FAILURE);
    			}

    			break;
    		default:
    			;
    		}


    		do {
            	pid_t ws = waitpid(child_pid, &wstatus, 0);
                if (ws == -1) {
                    perror("[parent] waitpid");
                    exit(EXIT_FAILURE);
                }

                if (WIFEXITED(wstatus)) {

                	modal_result = WEXITSTATUS(wstatus);

                    printf("[parent] child process è terminato, ha restituito: %d\n", modal_result);
                } else if (WIFSIGNALED(wstatus)) {
                    printf("[parent] child process killed by signal %d\n", WTERMSIG(wstatus));
                }
            } while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));

    		// fork

    		execl("/bin/sh", "sh", "-c", cmd, (char *) NULL);
    	}
    }
}


#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

char buf[BUF_LEN] __attribute__ ((aligned(__alignof__(struct inotify_event))));
// https://gcc.gnu.org/onlinedocs/gcc/Alignment.html
//

typedef char * mystr;

void monitor(mystr directories[], int directories_len) {


	int wd;
	int inotifyFd;
	int num_bytes_read;


	printf("NAME_MAX = %d\n", NAME_MAX);
	printf("sizeof(struct inotify_event) = %ld\n", sizeof(struct inotify_event));
	printf("__alignof__(struct inotify_event) = %ld bytes\n", __alignof__(struct inotify_event));
	printf("\n");



	char * cwd;

	cwd = getcwd(NULL, 0);

	printf("process current working directory: %s\n", cwd);

	free(cwd);

	if (directories_len == 0) {
		printf("provide at least a file or directory to watch!\n");
		exit(0);
	}

	// inotify_init() initializes a new inotify instance and
	// returns a file descriptor associated with a new inotify event queue.
    inotifyFd = inotify_init();
    if (inotifyFd == -1) {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

    // for each command line argument:
    for (int j = 0; j < directories_len; j++) {

    	if (directories[j] == NULL)
    		continue;

    	// inotify_add_watch()  adds  a  new  watch, or modifies an existing watch,
    	// for the file whose location is specified in pathname
        wd = inotify_add_watch(inotifyFd, directories[j], IN_ALL_EVENTS);
        if (wd == -1) {
            perror("inotify_init");
            exit(EXIT_FAILURE);
        }

        // we do not keep wd...

        printf("Watching %s using wd %d\n", directories[j], wd);
    }

    printf("ready!\n\n");

    // loop forever
    for (;;) {
    	num_bytes_read = read(inotifyFd, buf, BUF_LEN);
        if (num_bytes_read == 0) {
            printf("read() from inotify fd returned 0!");
            exit(EXIT_FAILURE);
        }

        if (num_bytes_read == -1) {

        	if (errno == EINTR) {
				printf("read(): EINTR\n");
				continue;
        	} else {
                perror("read()");
                exit(EXIT_FAILURE);
        	}
        }

        printf("read %d bytes from inotify fd\n", num_bytes_read);

        // process all of the events in buffer returned by read()

        struct inotify_event *event;

        for (char * p = buf; p < buf + num_bytes_read; ) {
            event = (struct inotify_event *) p;

            show_inotify_event(event);

            p += sizeof(struct inotify_event) + event->len;
            // event->len is length of (optional) file name
        }
    }


}



int main(int argc, char * argv[]) {

    int opt;

    mystr * dirs; // dynamic array
    int dirs_len = 0;
    int dirs_counter = 0;

    dirs = NULL;


    while ((opt = getopt(argc, argv, "d:s:")) != -1) {
        switch (opt) {
        case 'd':
        	if (dirs_counter >= dirs_len) {

        		dirs_len += 16;
        		dirs = realloc(dirs, sizeof(mystr) * dirs_len);

        	    if (dirs == NULL) {
        	        fprintf(stderr, "cannot reallocate array for files/directories to monitor\n");
        	        exit(EXIT_FAILURE);
        	    }

        	}

        	dirs[dirs_counter++] = optarg;
            break;
        case 's':
        	command = optarg;
            break;
        default: /* '?' */
            fprintf(stderr, "Usage: %s -d file/directory -s command\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }
//    printf("optind=%d\n", optind);


	printf("directory_counter=%d\n", dirs_counter);
	for (int i = 0; i < dirs_len; i++) {
		printf("directory[%d]: %s\n", i, dirs[i]);
	}

    printf("command: %s\n", command);


	if (command == NULL || strlen(command) > MAX_COMMAND_LEN) {
		fprintf(stderr, "invalid command\n");
		exit(EXIT_FAILURE);
	}

	if (dirs_len > 0) {
		monitor(dirs, dirs_len);
	}


	return EXIT_SUCCESS;
}
