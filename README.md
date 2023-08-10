# filemon

`filemon` is a program for monitoring one or more files or directories for the IN_CLOSE_WRITE event; 

for every event that is notified by the operating system, filemon invokes a command on it.


## Usage

`filemon` takes one or more -d parameters; each -d parameter requires the name of a file or directory to watch for events.

takes a single -c parameter which specifies the command to invoke when an event on the files/directories.

Whenever an event is recevied, the command is invoked, the absolute file name is concatenated to the command


## Example

 example: 
 ```bash
 filemon -d /tmp/ -c "ls -l"
 ```

with this command, `filemon` monitors directory `/tmp/`; when a `IN_CLOSE_WRITE` event is notified 
(for example: after a file is created and closed in the /tmp/ directory),
 the command `"ls -l"` is executed with the file as parameter


## Setup

build on linux:

```bash
gcc filemon.c -o   -s
```

install to /usr/bin/ directory: 
```bash
sudo cp filemon /usr/bin/
```


## run as a systemd service

`filemon` can be run as a systemd service 


## Limitations

`filemon` is being used on Debian 11/12 and Ubuntu 22 operating systems.
