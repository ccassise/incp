# incp
Insecure copy - copy files to a remote computer on a trusted network. Works on Windows, Linux, and macOS.

Files are transferred over a TCP connection as raw bytes. There is no encryption or compression being used. You probably want [scp](https://man.freebsd.org/cgi/man.cgi?query=scp&sektion=1&n=1), [rsync](https://man.freebsd.org/cgi/man.cgi?query=rsync&apropos=0&sektion=0&manpath=FreeBSD+8.0-RELEASE+and+Ports&format=html), [Copy-Item](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.management/copy-item?view=powershell-7.4), or maybe just piping a tar.gz through [netcat](https://man.freebsd.org/cgi/man.cgi?query=netcat&manpath=SuSE+Linux/i386+11.3).

## Usage
```
incp -l [PORT]
```
Listens on the optional port for a one time transfer of files. After the files have been transferred, the server shuts down. If no port is given, it will listen on the default port of 4627.
```
incp SOURCE [SOURCE...] <IPv4 ADDRESS>[:PORT]:DESTINATION
```
Attempt to transfer the source file(s) to the destination directory or file at the given address on the given port. If no port is given, it will attempt to connect to the default port of 4627. For the most part, this should work exactly like `cp` except the destination includes an IPv4 address. Currently, `incp` is only able to transfer files and not directories.

## Build
### Unix
```
$ make
```
### Windows
Open the Developer Powershell for Visual Studio and then change the directory to the project. `incp` uses the clang frontend for MSVC.
```
$ nmake /F Makefile-win
```

## File Permissions
`incp` will attempt to copy the file permissions from the source file if its destination does not already exist. If the destination exists, then permissions will not be modified. When transferring to or from Windows, only the user's read, write, and execute permissions are transferred. Group and other permissions are cleared.

## Protocol
![incp-sequence](https://github.com/ccassise/incp/assets/58533624/bb3510b4-9a40-467c-a4cf-e6313a822508)

`incp` uses a simple protocol to exchange files. The above diagram shows a successful transfer of files between remote computers. The protocol consists of three "types." Strings, file info, and then raw data.

Strings are text that ends in a CRLF, e.g.
```
HELLO\r\n
```

File info contains three space-separated columns. The columns are file type and file permission info, file size in bytes, and the file path. Everything after size is considered part of the path, so paths may contain spaces, e.g.
```
-rwxrw-rw- 1234 path/to/file
```

Raw data is only transferred after file info has been transferred. The raw data shall be exactly the same amount of bytes as was given in the file info size.
