/*
 ============================================================================
 Name        : filemon.c
 Author      : 
 Version     :
 Copyright   : Marco Tessarotto (c) 2023
 Description : monitors one or more files or directories specified as parameters; when a new event is notified, invokes an action on the file.
 it monitors the following inotify events: IN_CLOSE_WRITE

 example: filemon -d /tmp/ -c "ls -l"

 monitor directory tmp; when a IN_CLOSE_WRITE event is notified about a file in that directory, command is executed with new file as parameter


build on linux:
gcc filemon.c -o filemon -s
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

#include <syslog.h>


typedef char * char_p;

// stores command to execute on file
char * command = NULL;

// on linux, PATH_MAX is 4096
#define MAX_COMMAND_LEN (PATH_MAX*2)

char *space = " ";
char *slash = "/";

const char * FILEMON = "filemon";

static void show_inotify_event(struct inotify_event *i, char_p dir_name)
{
	syslog(LOG_INFO,"show_inotify_event [dir_name='%s' wd=%2d] ",dir_name, i->wd);

    if (i->cookie > 0)
    	syslog(LOG_DEBUG,"cookie=%4d ", i->cookie);

    // The  name  field is present only when an event is returned for a file
    // inside a watched directory; it identifies the filename within the watched directory.
    // This filename is null-terminated .....

    if (i->len > 0)
    	syslog(LOG_INFO,"show_inotify_event file name = %s ", i->name);
    else
    	syslog(LOG_INFO,"show_inotify_event *no file name* "); // event refers to watched directory

    // see man inotify
    // for explanation of events

    char mask_str[512] = "mask = ";

    // IN_ACCESS  File was accessed (e.g., read(2), execve(2)).
    if (i->mask & IN_ACCESS)        strcat(mask_str, "IN_ACCESS ");

    // IN_ATTRIB Metadata changed—for example, permissions, timestamps, user/group ID
    if (i->mask & IN_ATTRIB)        strcat(mask_str, "IN_ATTRIB ");

    if (i->mask & IN_CLOSE_NOWRITE) strcat(mask_str, "IN_CLOSE_NOWRITE ");

    // IN_CLOSE_NOWRITE  File or directory not opened for writing was closed.
    if (i->mask & IN_CLOSE_WRITE)   strcat(mask_str, "IN_CLOSE_WRITE ");

    if (i->mask & IN_CREATE)        strcat(mask_str, "IN_CREATE ");
    if (i->mask & IN_DELETE)        strcat(mask_str, "IN_DELETE ");
    if (i->mask & IN_DELETE_SELF)   strcat(mask_str, "IN_DELETE_SELF ");
    if (i->mask & IN_IGNORED)       strcat(mask_str, "IN_IGNORED ");

    // IN_ISDIR  Subject of this event is a directory.
    if (i->mask & IN_ISDIR)         strcat(mask_str, "IN_ISDIR ");

    if (i->mask & IN_MODIFY)        strcat(mask_str, "IN_MODIFY ");
    if (i->mask & IN_MOVE_SELF)     strcat(mask_str, "IN_MOVE_SELF ");
    if (i->mask & IN_MOVED_FROM)    strcat(mask_str, "IN_MOVED_FROM ");
    if (i->mask & IN_MOVED_TO)      strcat(mask_str, "IN_MOVED_TO ");

    // IN_OPEN  File or directory was opened.
    if (i->mask & IN_OPEN)          strcat(mask_str, "IN_OPEN ");

    if (i->mask & IN_Q_OVERFLOW)    strcat(mask_str, "IN_Q_OVERFLOW ");
    if (i->mask & IN_UNMOUNT)       strcat(mask_str, "IN_UNMOUNT ");

    syslog(LOG_INFO, "%s", mask_str);


    if ((i->mask & IN_CLOSE_WRITE)/* || (i->mask & IN_CLOSE_NOWRITE)*/) {
    	// execute command passing file as parameter
    	if (i->len) {

    		char cmd[PATH_MAX];
    		cmd[0] = 0;

    		// append command to cmd
    		strcat(cmd, command);

    		// append ' ' to cmd
    		strcat(cmd, space);

    		// append dir_name
    		strcat(cmd, dir_name);

    		// check if dir_name ends with slash symbol
    		if (dir_name[strlen(dir_name)] != '/') {
    			strcat(cmd, slash);
    		}

    		// append file name do cmd
    		strncat(cmd, i->name, i->len);

    		syslog(LOG_INFO, "cmd: %s", cmd);

    		pid_t child_pid;
    		int wstatus;
    		int modal_result = -1;

    		switch (child_pid = fork()) {
    		case -1:
    			perror("cannot fork");
    			exit(EXIT_FAILURE);
    		case 0:

    			child_pid = getpid();
    			syslog(LOG_INFO, "[child process] pid=%d", child_pid);

    			if (execl("/bin/sh", "sh", "-c", cmd, (char *) NULL) != 0) {
    				syslog(LOG_ERR, "[child process] execl");
    				exit(EXIT_FAILURE);
    			}

    			break;
    		default:
    			;
    		}


    		do {
            	pid_t ws = waitpid(child_pid, &wstatus, 0);
                if (ws == -1) {
                	syslog(LOG_ERR, "[parent] waitpid");
                    exit(EXIT_FAILURE);
                }

                if (WIFEXITED(wstatus)) {

                	modal_result = WEXITSTATUS(wstatus);

                	syslog(LOG_DEBUG, "[parent] child process has terminated, returning: %d", modal_result);
                } else if (WIFSIGNALED(wstatus)) {
                	syslog(LOG_DEBUG, "[parent] child process killed by signal %d", WTERMSIG(wstatus));
                }
            } while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));


    	}
    }
}


#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

char buf[BUF_LEN] __attribute__ ((aligned(__alignof__(struct inotify_event))));
// https://gcc.gnu.org/onlinedocs/gcc/Alignment.html


void monitor(char_p directories[], int directories_len) {

	int wd;
	int inotifyFd;
	int num_bytes_read;
	int * wd_names;

	wd_names = calloc(directories_len, sizeof(int));
	if (wd_names == NULL) {
		syslog(LOG_ERR, "calloc error");
        exit(EXIT_FAILURE);
	}

	// inotify_init() initializes a new inotify instance and
	// returns a file descriptor associated with a new inotify event queue.
    inotifyFd = inotify_init();
    if (inotifyFd == -1) {
    	syslog(LOG_ERR, "inotify_init");
        exit(EXIT_FAILURE);
    }

    // for each command line argument:
    for (int j = 0; j < directories_len; j++) {

    	if (directories[j] == NULL)
    		continue;

        syslog(LOG_INFO, "watching %s", directories[j]);

    	// inotify_add_watch()  adds  a  new  watch, or modifies an existing watch,
    	// for the file whose location is specified in pathname
        wd = inotify_add_watch(inotifyFd, directories[j], IN_ALL_EVENTS);
        if (wd == -1) {
        	syslog(LOG_ERR, "inotify_init");
            exit(EXIT_FAILURE);
        }

        // associate watch descriptor to position of name in the array of strings
        wd_names[j] = wd;

    }

    syslog(LOG_INFO, "ready!");

    // loop forever
    for (;;) {
    	num_bytes_read = read(inotifyFd, buf, BUF_LEN);
        if (num_bytes_read == 0) {
        	syslog(LOG_ERR, "read() from inotify fd returned 0!");
            exit(EXIT_FAILURE);
        }

        if (num_bytes_read == -1) {

        	if (errno == EINTR) {
        		syslog(LOG_ERR, "read(): EINTR");
				continue;
        	} else {
        		syslog(LOG_ERR, "read()");
                exit(EXIT_FAILURE);
        	}
        }

        syslog(LOG_DEBUG, "read %d bytes from inotify fd", num_bytes_read);

        // process all of the events in buffer returned by read()

        struct inotify_event *event;

        for (char * p = buf; p < buf + num_bytes_read; ) {
            event = (struct inotify_event *) p;

            // recover directory name associated to wd
            int dir_pos = -1;
            for (int i = 0; i < directories_len; i++) {
            	if (wd_names[i] == event->wd) {
            		dir_pos = i;
            		break;
            	}
            }

            if (dir_pos == -1) {
            	syslog(LOG_ERR, "cannot find directory name!");
            	exit(EXIT_FAILURE);
            }

            show_inotify_event(event, directories[dir_pos]);

            p += sizeof(struct inotify_event) + event->len;
            // event->len is length of (optional) file name
        }
    }


}


void show_help(int argc, char * argv[]) {
	fprintf(stderr, "monitors one or more files or directories specified as parameters; when a new file is detected, invokes an action on it.\n");
    fprintf(stderr, "Usage: %s -d file/directory -c command\n", argv[0]);
    fprintf(stderr, "example: %s -d /tmp/ -d /home/marco/ -c \"ls -l\"\n", argv[0]);
}



int main(int argc, char * argv[]) {

    int opt;

    char_p * dirs; // dynamic array
    int dirs_len = 0;
    int dirs_counter = 0;

    // array of strings containing absolute path of directories to monitor
    char_p * abs_dirs;

    dirs = NULL;

    openlog(FILEMON, LOG_CONS | LOG_PERROR | LOG_PID, 0);

    while ((opt = getopt(argc, argv, "d:c:")) != -1) {
        switch (opt) {
        case 'd':
        	if (dirs_counter >= dirs_len) {

        		dirs_len += 16;
        		dirs = realloc(dirs, sizeof(char_p) * dirs_len);

        	    if (dirs == NULL) {
        	    	syslog(LOG_ERR, "cannot reallocate array for files/directories to monitor\n");
        	        exit(EXIT_FAILURE);
        	    }

        	}

        	dirs[dirs_counter++] = optarg;
            break;
        case 'c':
        	command = optarg;
            break;
        default: /* '?' */
        	show_help(argc, argv);

            exit(EXIT_FAILURE);
        }
    }

    if (command == NULL || dirs_len == 0) {
    	show_help(argc, argv);
    	exit(EXIT_FAILURE);
    }


	syslog(LOG_INFO,"command: %s", command);

    syslog(LOG_INFO,"number of specified files/directories: %d", dirs_counter);
	for (int i = 0; i < dirs_len; i++) {
		if (dirs[i] != NULL)
			syslog(LOG_INFO,"directory[%d]: %s", i, dirs[i]);
	}

	if (strlen(command) > MAX_COMMAND_LEN) {
		syslog(LOG_ERR, "invalid command");
		exit(EXIT_FAILURE);
	}

	if (dirs_len > 0) {

		// transform paths to absolute paths
		abs_dirs = calloc(dirs_len, sizeof(char_p));
		if (abs_dirs == NULL) {
			syslog(LOG_ERR, "cannot allocate array for files/directories to monitor");
	        exit(EXIT_FAILURE);
		}

		for (int i = 0; i < dirs_len; i++) {
			if (dirs[i] == NULL)
				continue;

			abs_dirs[i] = calloc(PATH_MAX, sizeof(char));
			if (abs_dirs[i] == NULL) {
				syslog(LOG_ERR, "cannot allocate array for files/directories to monitor");
		        exit(EXIT_FAILURE);
			}

			if (realpath(dirs[i], abs_dirs[i]) == NULL) {
				syslog(LOG_ERR, "error in calculating absolute path");
				exit(EXIT_FAILURE);
			}
		}

		monitor(abs_dirs, dirs_len);
	}


	return EXIT_SUCCESS;
}
