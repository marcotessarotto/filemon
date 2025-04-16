/*
 ============================================================================
 Name        : filemon.c
 Author      : 
 Version     :
 Copyright   : Marco Tessarotto (c) 2023
 Description : Monitors one or more files or directories specified as parameters;
 when an IN_CLOSE_WRITE or IN_MOVED_TO event is notified, invokes a command on that file.
 Example: filemon -d /tmp/ -c "ls -l"
 ============================================================================
 */

#include <sys/types.h>   /* Type definitions used by many programs */
#include <stdio.h>       /* Standard I/O functions */
#include <stdlib.h>      /* Prototypes of commonly used library functions */
#include <unistd.h>      /* Prototypes for many system calls */
#include <errno.h>       /* Declares errno and defines error constants */
#include <string.h>      /* String handling functions */
#include <stdbool.h>     /* 'bool' type plus 'true' and 'false' constants */
#include <signal.h>      /* Signal handling */
#include <sys/inotify.h>
#include <limits.h>
#include <sys/wait.h>
#include <syslog.h>

typedef char *char_p;

#define MAX_COMMAND_LEN (PATH_MAX * 2)

// Global variables for command and static strings
char *command = NULL;
char *space = " ";
char *slash = "/";
const char *FILEMON = "filemon";

// Flag for graceful shutdown
volatile sig_atomic_t running = 1;

void handle_signal(int sig) {
	running = 0;
}

// Displays an inotify event and, in the case of an event that signals file arrival,
// executes a command passing the file as parameter.
static void show_inotify_event(struct inotify_event *i, char_p dir_name) {
	syslog(LOG_INFO, "show_inotify_event [dir_name='%s' wd=%2d]", dir_name,
			i->wd);

	if (i->cookie > 0)
		syslog(LOG_DEBUG, "cookie=%4d", i->cookie);

	if (i->len > 0)
		syslog(LOG_INFO, "show_inotify_event file name = %s", i->name);
	else
		syslog(LOG_INFO, "show_inotify_event *no file name*");

	// Build mask string for logging purposes
	char mask_str[512] = "mask = ";
	if (i->mask & IN_ACCESS)
		strcat(mask_str, "IN_ACCESS ");
	if (i->mask & IN_ATTRIB)
		strcat(mask_str, "IN_ATTRIB ");
	if (i->mask & IN_CLOSE_NOWRITE)
		strcat(mask_str, "IN_CLOSE_NOWRITE ");
	if (i->mask & IN_CLOSE_WRITE)
		strcat(mask_str, "IN_CLOSE_WRITE ");
	if (i->mask & IN_CREATE)
		strcat(mask_str, "IN_CREATE ");
	if (i->mask & IN_DELETE)
		strcat(mask_str, "IN_DELETE ");
	if (i->mask & IN_DELETE_SELF)
		strcat(mask_str, "IN_DELETE_SELF ");
	if (i->mask & IN_IGNORED)
		strcat(mask_str, "IN_IGNORED ");
	if (i->mask & IN_ISDIR)
		strcat(mask_str, "IN_ISDIR ");
	if (i->mask & IN_MODIFY)
		strcat(mask_str, "IN_MODIFY ");
	if (i->mask & IN_MOVE_SELF)
		strcat(mask_str, "IN_MOVE_SELF ");
	if (i->mask & IN_MOVED_FROM)
		strcat(mask_str, "IN_MOVED_FROM ");
	if (i->mask & IN_MOVED_TO)
		strcat(mask_str, "IN_MOVED_TO ");
	if (i->mask & IN_OPEN)
		strcat(mask_str, "IN_OPEN ");
	if (i->mask & IN_Q_OVERFLOW)
		strcat(mask_str, "IN_Q_OVERFLOW ");
	if (i->mask & IN_UNMOUNT)
		strcat(mask_str, "IN_UNMOUNT ");
	syslog(LOG_INFO, "%s", mask_str);

	// Process only events that indicate a file has been written or moved into the directory.
	if (((i->mask & IN_CLOSE_WRITE) || (i->mask & IN_MOVED_TO)) && i->len > 0) {
		// Ignore temporary files (which by convention begin with a dot)
		if (i->name[0] == '.') {
			syslog(LOG_DEBUG, "Ignoring temporary file: %s", i->name);
			return;
		}

		char cmd[MAX_COMMAND_LEN];
		int ret = snprintf(cmd, sizeof(cmd), "%s %s%s%s", command, dir_name,
				(dir_name[strlen(dir_name) - 1] == '/') ? "" : slash, i->name);
		if (ret < 0 || ret >= sizeof(cmd)) {
			syslog(LOG_ERR, "Command buffer overflow");
			return;
		}

		syslog(LOG_INFO, "cmd: %s", cmd);

		pid_t child_pid;
		int wstatus;
		int child_exit_status = -1;

		child_pid = fork();
		if (child_pid == -1) {
			perror("cannot fork");
			exit(EXIT_FAILURE);
		} else if (child_pid == 0) {
			syslog(LOG_INFO, "[child process] pid=%d", getpid());
			if (execl("/bin/sh", "sh", "-c", cmd, (char*) NULL) == -1) {
				syslog(LOG_ERR, "[child process] execl failed");
				exit(EXIT_FAILURE);
			}
		} else {
			do {
				pid_t ws = waitpid(child_pid, &wstatus, 0);
				if (ws == -1) {
					syslog(LOG_ERR, "[parent] waitpid error");
					exit(EXIT_FAILURE);
				}
			} while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));

			if (WIFEXITED(wstatus)) {
				child_exit_status = WEXITSTATUS(wstatus);
				syslog(LOG_DEBUG,
						"[parent] child process terminated, exit status: %d",
						child_exit_status);
			} else if (WIFSIGNALED(wstatus)) {
				syslog(LOG_DEBUG, "[parent] child process killed by signal %d",
						WTERMSIG(wstatus));
			}
		}
	}
}

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))
char buf[BUF_LEN] __attribute__ ((aligned(__alignof__(struct inotify_event))));

// Monitors the provided directories for events.
void monitor(char_p directories[], int directories_count) {
	int wd;
	int inotifyFd;
	int num_bytes_read;
	int *wd_names;

	wd_names = calloc(directories_count, sizeof(int));
	if (wd_names == NULL) {
		syslog(LOG_ERR, "calloc error");
		exit(EXIT_FAILURE);
	}

	inotifyFd = inotify_init();
	if (inotifyFd == -1) {
		syslog(LOG_ERR, "inotify_init failed");
		exit(EXIT_FAILURE);
	}

	// Watch each provided directory.
	for (int j = 0; j < directories_count; j++) {
		if (directories[j] == NULL)
			continue;
		syslog(LOG_INFO, "watching %s", directories[j]);
		wd = inotify_add_watch(inotifyFd, directories[j], IN_ALL_EVENTS);
		if (wd == -1) {
			syslog(LOG_ERR, "inotify_add_watch failed for %s", directories[j]);
			exit(EXIT_FAILURE);
		}
		wd_names[j] = wd;
	}

	syslog(LOG_INFO, "ready!");

	// Main event loop – exits gracefully if a signal is caught.
	while (running) {
		num_bytes_read = read(inotifyFd, buf, BUF_LEN);
		if (num_bytes_read == 0) {
			syslog(LOG_ERR, "read() from inotify fd returned 0!");
			break;
		}
		if (num_bytes_read == -1) {
			if (errno == EINTR) {
				syslog(LOG_ERR, "read() interrupted by signal");
				continue;
			} else {
				syslog(LOG_ERR, "read() error");
				exit(EXIT_FAILURE);
			}
		}
		syslog(LOG_DEBUG, "read %d bytes from inotify fd", num_bytes_read);

		for (char *p = buf; p < buf + num_bytes_read;) {
			struct inotify_event *event = (struct inotify_event*) p;

			int dir_pos = -1;
			for (int i = 0; i < directories_count; i++) {
				if (wd_names[i] == event->wd) {
					dir_pos = i;
					break;
				}
			}
			if (dir_pos == -1) {
				syslog(LOG_ERR, "cannot find directory name for wd %d",
						event->wd);
				exit(EXIT_FAILURE);
			}
			show_inotify_event(event, directories[dir_pos]);
			p += sizeof(struct inotify_event) + event->len;
		}
	}

	free(wd_names);
	close(inotifyFd);
}

void show_help(int argc, char *argv[]) {
	fprintf(stderr,
			"Monitors one or more files or directories specified as parameters; when a file event (IN_CLOSE_WRITE or IN_MOVED_TO) is detected, executes an action on it.\n");
	fprintf(stderr, "Usage: %s -d file/directory -c command\n", argv[0]);
	fprintf(stderr, "Example: %s -d /tmp/ -d /home/marco/ -c \"ls -l\"\n",
			argv[0]);
}

int main(int argc, char *argv[]) {
	int opt;
	char_p *dirs = NULL;    // Dynamic array for directory strings
	int dirs_len = 0;        // Allocated length (capacity)
	int dirs_counter = 0;    // Actual number of directories provided

	openlog(FILEMON, LOG_CONS | LOG_PERROR | LOG_PID, 0);

	// Register signal handlers for graceful termination.
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	while ((opt = getopt(argc, argv, "d:c:")) != -1) {
		switch (opt) {
		case 'd':
			if (dirs_counter >= dirs_len) {
				dirs_len += 16;
				char_p *tmp = realloc(dirs, sizeof(char_p) * dirs_len);
				if (tmp == NULL) {
					syslog(LOG_ERR,
							"cannot reallocate array for files/directories to monitor");
					exit(EXIT_FAILURE);
				}
				dirs = tmp;
			}
			dirs[dirs_counter++] = optarg;
			break;
		case 'c':
			command = optarg;
			break;
		default:
			show_help(argc, argv);
			exit(EXIT_FAILURE);
		}
	}

	if (command == NULL || dirs_counter == 0) {
		show_help(argc, argv);
		exit(EXIT_FAILURE);
	}

	syslog(LOG_INFO, "command: %s", command);
	syslog(LOG_INFO, "number of specified files/directories: %d", dirs_counter);
	for (int i = 0; i < dirs_counter; i++) {
		if (dirs[i] != NULL)
			syslog(LOG_INFO, "directory[%d]: %s", i, dirs[i]);
	}

	if (strlen(command) > MAX_COMMAND_LEN) {
		syslog(LOG_ERR, "invalid command length");
		exit(EXIT_FAILURE);
	}

	// Convert paths to absolute paths.
	char_p *abs_dirs = calloc(dirs_counter, sizeof(char_p));
	if (abs_dirs == NULL) {
		syslog(LOG_ERR,
				"cannot allocate array for files/directories to monitor");
		exit(EXIT_FAILURE);
	}
	for (int i = 0; i < dirs_counter; i++) {
		if (dirs[i] == NULL)
			continue;
		abs_dirs[i] = calloc(PATH_MAX, sizeof(char));
		if (abs_dirs[i] == NULL) {
			syslog(LOG_ERR, "cannot allocate memory for absolute path");
			exit(EXIT_FAILURE);
		}
		if (realpath(dirs[i], abs_dirs[i]) == NULL) {
			syslog(LOG_ERR, "error calculating absolute path for %s", dirs[i]);
			exit(EXIT_FAILURE);
		}
	}

	monitor(abs_dirs, dirs_counter);

	// Cleanup: free absolute directory array.
	for (int i = 0; i < dirs_counter; i++) {
		free(abs_dirs[i]);
	}
	free(abs_dirs);

	closelog();
	return EXIT_SUCCESS;
}
