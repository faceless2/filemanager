class FileManager {

    #view;
    #info;
    #trash;

    constructor(elt, cgi, config) {
        const self = this;
        if (!elt.id) {
            elt.id = "filemanager-" + (Math.random() + 1).toString(36).substring(7);
        }
        this.elt = elt;
        this.cgi = cgi;
        this.elt.classList.add("filemanager");
        this.chdir("/");
        this.config = config || {};

        const view = document.createElement("div");
        view.classList.add("filemanager-view");
        this.elt.appendChild(view);

        const info = document.createElement("div");
        info.classList.add("filemanager-status");
        info.appendChild(document.createElement("span"));
        info.appendChild(document.createElement("progress"));
        this.elt.appendChild(info);

        const trash = document.createElement("div");
        trash.classList.add("filemanager-trash");
        trash.classList.add("hidden");
        this.elt.appendChild(trash);

        view.addEventListener("dragenter", (e) => {
            if (self.#trash.classList.contains("hidden")) {
                view.classList.add("dragover");
                e.dataTransfer.dropEffect = "move";
            }
        });
        view.addEventListener("dragleave", (e) => {
            if (self.#trash.classList.contains("hidden")) {
                if (document.elementsFromPoint(e.clientX, e.clientY).includes(view)) {
                    return; // Related target shold work but doesn't on Safari; this works everywhere
                }
                view.classList.remove("dragover");
            }
        });
        view.addEventListener("dragover", (e) => {
            e.preventDefault();
        });
        view.addEventListener("drop", function(e) {
            view.classList.remove("dragover");
            e.preventDefault();
            if (view.hasAttribute("data-path") && !view.hasAttribute("data-readonly")) {
                const path = view.getAttribute("data-path");
                self.#trash.classList.add("hidden");
                let files = [];
                Array.from(e.dataTransfer.items).forEach((e) => {
                    if (e.kind == "file") {
                        e = e.webkitGetAsEntry();
                        if (e) {
                            e.target = path;
                            files.push(e);
                        }
                    }
                });
                self.#loader(files, 0, null, 0);
             }
         });
         trash.addEventListener("dragover", (e) => {
             e.preventDefault();
             e.dataTransfer.dropEffect = "move";
         });
         trash.addEventListener("drop", (e) => {
             e.preventDefault();
             trash.classList.add("hidden");
             const id = e.dataTransfer.getData("text/plain");
             if (id.startsWith(self.elt.id + "-path-")) {
                 const node = document.getElementById(id);
                 self.deleteItem(node.getAttribute("data-path"));
             }
         });
        this.#view = view;
        this.#info = info;
        this.#trash = trash;
    }

    /**
     * class - class to add on o
     * progress - if non-zero, a new item isn't created
     * text - the text
     * progress - if a number, the progress from 0..1
     */
    log(msg, type, progress) {
        console.log(type ? "[" + type + "] " + msg : msg);
        const info = this.#info;
        const span = info.querySelector("span");
        const progresselt = info.querySelector("progress");
        span.innerHTML = msg;
        if (type) {
            span.classList.add(type);
            span.setAttribute("data-type", type);
        } else if (span.hasAttribute("data-type")) {
            span.classList.remove(span.getAttribute("data-type"));
            span.removeAttribute("data-type");
        }
        if (progress > 0) {
            progresselt.setAttribute("max", 100);
            progresselt.setAttribute("value", Math.min(100, progress * 100));
        } else {
            progresselt.removeAttribute("value");
        }
    }

    /**
     * @param files a list of FileSystemEntry objects
     * @param fileIndex the item from files to process
     * @param content the content of the current file (or null initially)
     * @param chunkIndex which chunk is being uploaded (0 initially)
     * @callback when everything is processed, an optional function to callback
     */
    #loader(files, fileIndex, content, chunkIndex, callback) {
        try {
            const self = this;
            const viewpath = self.#view.getAttribute("data-path");
            //console.log("#loader: " + fileIndex+"/"+content+"/"+chunkIndex);
            const chunkSize = 32768;
            if (!content) {
                if (fileIndex < files.length) {
                    const file = files[fileIndex];
                    if (file.isFile) {
                        file.file((f) => {
                            f.arrayBuffer().then((e) => {
                                self.#loader(files, fileIndex, e, 0, callback);
                            }).catch((e) => {
                                self.log("Load failed with " + e.message, "error");
                                self.#loader(files, ++fileIndex, null, 0, callback);
                            });
                        });
                    } else if (file.isDirectory) {
                        let target = file.target + file.name + "/";
                        file.createReader().readEntries((list) => {
                            list.forEach((subfile) => {
                                subfile.target = target;
                            });
                            self.#loader(list, 0, null, 0, () => {
                                self.refresh([file.target + file.name]);
                                self.#loader(files, ++fileIndex, null, 0, callback);
                            });
                        });
                    }
                } else {
                    self.log(viewpath, "breadcrumb");
                    if (callback) {
                        callback();
                    }
                }
            } else {
                const file = files[fileIndex];
                const target = file.target;
                const name = target + file.name;
                const chunk = content.slice(chunkIndex * chunkSize, Math.min(content.byteLength, (chunkIndex + 1) * chunkSize));
                let uri = self.cgi + "/put?path=" + encodeURIComponent(name) + "&off=" + (chunkSize * chunkIndex);
                console.log("Tx " + uri);
                fetch(uri, {
                    "method": "POST",
                    "headers": {
                        "content-type": "application/octet-stream",
                        "content-length": chunk.length
                    },
                    "body": chunk
                }).then((r) => r.json().then((r) => {
                    console.log("Rx " + uri);
                    if (r.ok) {
                        chunkIndex++;
                        let progress = chunkIndex * chunkSize / content.byteLength;
                        self.log("Uploading \"" + name + "\"", null, progress);
                        if (progress >= 1) {
                            self.refresh([name]);
                            self.#loader(files, ++fileIndex, null, 0, callback);
                        } else {
                            self.#loader(files, fileIndex, content, chunkIndex, callback);
                        }
                    } else {
                        self.log("Upload failed with " + r.status, "error");
                        self.#loader(files, ++fileIndex, null, 0, callback);
                    }
                }));
            }
        } catch (e) {
            console.log(e);
        }
    }

    #newitem(props) {
        const self = this;
        const view = this.#view;
        const id = this.elt.id + "-path-" + props.path;
        let e = document.getElementById(id);
        if (!e) {
            e = document.createElement("div");
            e.id = id;
        }
        let dp = {};
        for (const [key, value] of Object.entries(props)) {
            if (key != "kids" && key != "draggable") {
                dp["data-" + key] = String(value);
            }
        }
        let changed = false;
        for (let name of e.getAttributeNames()) {
            if (name.startsWith("data-") && dp[name] != e.getAttribute(name)) {
                e.removeAttribute(name);
                changed = true;
            } else {
                delete dp[name];
            }
        }
        for (const [key, value] of Object.entries(dp)) {
            e.setAttribute(key, value);
            changed = true;
        }
        if (changed) {
            while (e.firstChild) {
                e.firstChild.remove();
            }
            let e2 = document.createElement("div");
            e2.classList.add("icon");
            e.appendChild(e2);
            e2 = document.createElement("div");
            e2.classList.add("name");
            let name = props.name;
            if (!name) {
                name = props.path.replace(/.*\//, "");
            }
            e2.innerHTML = name;
            e.appendChild(e2);
            e.addEventListener("click", () => {
               if (props.type == "dir") {
                   self.chdir(props.path);
               } else {
                   let a = document.createElement("a");
                   a.href = this.cgi + "/get?path=" + props.path;
                   a.download = name;
                   a.style.display = "none";
                   document.body.appendChild(a);
                   setTimeout(() => {
                       a.click();
                       setTimeout(() => {
                           a.remove();
                       }, 0);
                   }, 0);
               }
            });
            if (props.name != "..") {
                e.setAttribute("draggable", "true");
                e.addEventListener("dragstart", (evt) => {
                    self.#trash.classList.remove("hidden");
                    evt.dataTransfer.setData("text/plain", id);
                });
            }
        }
        if (!e.parentNode) {
            if (this.config.comparator) {
                for (let n=view.firstChild;n;n=n.nextSibling) {
                    let x = this.config.comparator(e, n);
                    if (x < 0) {
                        view.insertBefore(e, n);
                        break;
                    }
                }
            }
            if (!e.parentNode) {
                view.appendChild(e);
            }
        }
    }

    deleteItem(path) {
        const self = this;
        let uri = self.cgi + "/delete?path=" + encodeURIComponent(path);
        console.log("Tx" + uri);
        fetch(uri, {}).then((res) => res.json()).then((r) => {
            console.log("Rx " + JSON.stringify(r));
            self.refresh(path);
        });
    }

    refresh(names) {
        const self = this;
        const view = self.#view;
        if (!Array.isArray(names)) {
            names = [names];
        }
        const path = view.getAttribute("data-path");
        let uri = null;
        let sent = [];
        for (let i=0;i<names.length;i++) {
            let name = names[i];
            if (typeof name == "string") {
                if (name[0] != '/') {
                    name = path + name;
                }
                if (name.startsWith(path) && name.substring(path.length).indexOf("/") < 0) {
                    sent.push(name);
                    uri = (uri ? uri + "&" : self.cgi + "/info?") + "path="+encodeURIComponent(name);
                    if (uri.length > 500 || i + 1 == names.length) {
                        console.log("Tx " + uri);
                        fetch(uri, {}).then((res) => res.json()).then((r) => {
                            console.log("Rx " + JSON.stringify(r));
                            if (r.ok) {
                                for (let props of r.paths) {
                                    self.#newitem(props);
                                    sent = sent.filter(item => item !== props.path)
                                }
                                for (let name of sent) {
                                    const id = self.elt.id + "-path-" + name;
                                    let e = document.getElementById(id);
                                    if (e) {
                                        e.remove();
                                    }
                                }
                            }
                        });
                    }
                }
            }
        }
    }

    chdir(path) {
        const self = this;
        fetch(self.cgi + "/info?path=" + encodeURIComponent(path), {}).then((res) => res.json()).then((r) => {
            console.log("Rx " + JSON.stringify(r));
            const view = self.#view;
            if (r.ok && r.paths[0].type == "dir") {
                r = r.paths[0];
                let oldpath = view.getAttribute("data-path");
                for (let name of view.getAttributeNames()) {
                    if (name.startsWith("data-")) {
                        view.removeAttribute(name);
                    }
                }
                for (let [key, value] of Object.entries(r)) {
                    if (key == "path" && !value.endsWith("/")) {
                        value += "/";
                    }
                    if (key != "kids") {
                        view.setAttribute("data-" + key, value);
                    }
                }
                const path = view.getAttribute("data-path");    // Always ends with slash
                self.log(path, "breadcrumb");
                if (path != oldpath) {
                    while (view.firstChild) {
                        view.firstChild.remove();
                    }
                }
                if (path != "/") {
                    self.#newitem({
                      type: "dir",
                      path: path.substring(0, path.lastIndexOf("/", path.length - 2) + 1),
                      name: ".."
                    });
                }
                if (r.kids.length) {
                    self.refresh(r.kids);
                }
            }
        });
    }
}
