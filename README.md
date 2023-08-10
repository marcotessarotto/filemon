# filemon

`filemon` is a program for monitoring one or more files or directories for the IN_CLOSE_WRITE event; 

for every event that is notified by the operating system, filemon invokes a command on it.


## Usage

`filemon` requires at least one -d parameter and one -c parameter:

- one or more `-d file_or_directory` parameter; each -d parameter requires the name of a file or directory to watch for events.
- a single `-c command` parameter which specifies the command to invoke when an event on the files/directories.

Whenever an event is notified to `filemon`, the command is invoked, the absolute file name is concatenated to the command.


## Example

 example: 
 ```bash
 filemon -d /tmp/ -c "ls -l"
 ```

with this command, `filemon` monitors directory `/tmp/`; when a `IN_CLOSE_WRITE` event is notified 
(for example: after a file is created and closed in the /tmp/ directory),
 the command `"ls -l"` is executed with the file as parameter
 
 output running the example, and running the command  `touch /tmp/this_is_a_new_file` in a separate console:
 
 ```bash 
$ filemon -d /tmp/ -c "ls -l"
filemon[9714]: command: ls -l
filemon[9714]: number of specified files/directories: 1
filemon[9714]: directory[0]: /tmp/
filemon[9714]: watching /tmp
filemon[9714]: ready!
filemon[9714]: read 192 bytes from inotify fd
filemon[9714]: show_inotify_event [dir_name='/tmp' wd= 1] 
filemon[9714]: show_inotify_event file name = this_is_a_new_file 
filemon[9714]: mask = IN_CREATE 
filemon[9714]: show_inotify_event [dir_name='/tmp' wd= 1] 
filemon[9714]: show_inotify_event file name = this_is_a_new_file 
filemon[9714]: mask = IN_OPEN 
filemon[9714]: show_inotify_event [dir_name='/tmp' wd= 1] 
filemon[9714]: show_inotify_event file name = this_is_a_new_file 
filemon[9714]: mask = IN_ATTRIB 
filemon[9714]: show_inotify_event [dir_name='/tmp' wd= 1] 
filemon[9714]: show_inotify_event file name = this_is_a_new_file 
filemon[9714]: mask = IN_CLOSE_WRITE 
filemon[9714]: cmd: ls -l /tmp/this_is_a_new_file
filemon[9978]: [child process] pid=9978
-rw-r--r-- 1 marco marco 0 Aug 10 11:50 /tmp/this_is_a_new_file
filemon[9714]: [parent] child process has terminated, returning: 0
 ```
 


## Setup

build on linux:

```bash
gcc filemon.c -o filemon -s
```

install to /usr/bin/ directory: 
```bash
sudo cp filemon /usr/bin/
```

## Development
`filemon` is developed using [Eclipse IDE for C/C++ Developers](https://www.eclipse.org/downloads/packages/release/2023-06/r/eclipse-ide-cc-developers).
The repository contains all project files.



## Run as a systemd service

`filemon` can be run as a systemd service.

Here is a sample service configuration file:

```bash
[Unit]
Description=filemon service

[Service]
User=marco
Group=marco
ExecStart=/usr/bin/filemon -d /home/marco/directory/ -c /home/marco/process_file.sh

[Install]
WantedBy=multi-user.target
```

In this example, `filemon` monitors the `/home/marco/directory/` directory; 
when the IN_CLOSE_WRITE event regarding a file in the directory is notified to `filemon`,
it invokes the `/home/marco/process_file.sh` command on the file which is passed as a parameter to the command.


## Limitations

`filemon` is being used and tested on Debian 11/12 and Ubuntu 20/22 operating systems.
