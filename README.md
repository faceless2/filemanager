# Simple HTML File Manager

An intentionally trivial HTML interface for uploading and downloading files to/from a webserver

* Can upload files and (recursively) entire directories by dragging them into the window.
* Click on a directory to traverse into it, on a file to download it
* Drag a file or directory to the trash to delete it
* Makes no assumptions about frameworks, and designed to make it easy to restyle.
* Doesn't try to do anything other than upload, download and delete files.
  
There are many HTML file managers, most of which make assumptions about the server-side framework. This one is a single CGI script written in POSIX C which requires only the `jansson` library to build (the root folder for uploads can be compiled in, or it can be specified by an environment variable). Alternatively you could write a replacement, so long as it speaks the same trivial wire protocol.

https://github.com/user-attachments/assets/64a09ecf-92e5-475d-af4f-86cf833dfc82


## Protocol
The wire protocol is in JSON. Sample communications:

The `info` command retrieves the details of one or more paths. For this command only if `path` is missing, it defaults to '/'. Path segments beginning with "." are invalid for all commands, this includes ".." so there is no escaping the root directory:
```
GET /filemanager.cgi/info

HTTP/1.0 200 OK
Content-type: application/json
{
 "ok":true,
 "paths":[
  {
   "path":"/",
   "type":"dir",
   "ctime":1755860087,
   "mtime":1755860087,
   "kids":["file1.pdf","subdirectory"]
  }
 ]
}

GET /filemanager.cgi/info?path=/file1.pdf&path=/subdirectory

HTTP/1.0 200 OK
Content-type: application/json
{
 "ok":true,
 "paths":[
  {
   "path":"/file1.pdf",
   "type":"file",
   "length":12345,
   "readonly":true,
   "ctime":1755860087,
   "mtime":1755860087
  },{
   "path":"/subdirectory",
   "type":"dir",
   "readonly":true,
   "ctime":1755860087,
   "mtime":1755860087,
   "kids":["file2.png","file2.pdf","file3.pdf"]
  }
 ]
}
```

The `get` command retrieves the file. The supplied CGI always returns them as an octet-stream so they're downloaded rather than displayed.
```
GET /filemanager.cgi/get?path=/file1.pdf

HTTP/1.0 200 OK
Content-type: application/octet-stream
Content-length: nnn

...
```

The `put` command uploads a chunk of a file. If the offset is 0 or missing, the file is created along with any required parent directories. Otherwise the offset must match the current length of the file:
```
POST /filemanager.cgi/put?path=/subdirectory/file2.pdf&off=0
Content-Length: 32768

HTTP/1.0 200 OK
Content-type: application/json
{ok: true, msg: "wrote 32768 bytes"}

POST /filemanager.cgi/put?path=/subdirectory/file2.pdf&off=32768
Content-Length: 10000

HTTP/1.0 200 OK
Content-type: application/json
{ok: true, msg: "wrote 10000 bytes"}

... etc until the file is complete
```

The `delete` path recursively removes the path, whether it is a file or directory. All files/directories must be writable and permissions for all of them are checked recursiively before any deletions start. A list of all the deleted paths are returned in the reply.


```
GET /filemanager.cgi/delete?path=/subdirectory

HTTP/1.0 200 OK
Content-type: application/json
{ok: true, paths: ["/subdirectory/file2.pdf", "/subdirectory"]}

```
