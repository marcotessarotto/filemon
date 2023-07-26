# filemon
file monitor:

monitors one or more files or directories specified as parameters; when a new file is detected, invokes an action on it.

monitors the following inotify events: IN_CLOSE_NOWRITE, IN_CLOSE_WRITE

 example: filemon -d /tmp/ -c "ls -l"

 monitor directory tmp; when a file is created, command is executed with new file as parameter


build on linux:
gcc filemon.c -o filemon -s
